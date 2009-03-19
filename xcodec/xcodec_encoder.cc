#include <common/buffer.h>
#include <common/endian.h>

#include <xcodec/xcbackref.h>
#include <xcodec/xcdb.h>
#include <xcodec/xchash.h>
#include <xcodec/xcodec.h>
#include <xcodec/xcodec_encoder.h>

struct xcodec_special_p {
	bool operator() (uint8_t ch) const
	{
		return (XCODEC_CHAR_SPECIAL(ch));
	}
};

XCodecEncoder::Data::Data(void)
: prefix_(),
  hash_(),
  seg_(NULL)
{ }

XCodecEncoder::Data::Data(const XCodecEncoder::Data& src)
: prefix_(src.prefix_),
  hash_(src.hash_),
  seg_(NULL)
{
	if (src.seg_ != NULL) {
		src.seg_->ref();
		seg_ = src.seg_;
	}
}

XCodecEncoder::Data::~Data()
{
	if (seg_ != NULL) {
		seg_->unref();
		seg_ = NULL;
	}
}


XCodecEncoder::XCodecEncoder(XCodec *codec)
: log_("/xcodec/encoder"),
  database_(codec->database_),
  backref_()
{ }

XCodecEncoder::~XCodecEncoder()
{ }

/*
 * This takes a view of a data stream and turns it into a series of references
 * to other data, declarations of data to be referenced, and data that needs
 * escaped.
 *
 * It's very simple and aggressive and performs worse than the original encoder
 * as a result.  The old encoder did a few important things differently.  First,
 * it would start declaring data as soon as we knew we hadn't found a hash for a
 * given chunk of data.  What I mean is that it would look at a segment and a
 * segment but one worth of data until it found either a match or something that
 * didn't collide, and then it would use that.
 *
 * Secondly, as a result of that, it didn't need to do two database lookups, and
 * those things get expensive.
 *
 * However, we have a few places where we can do better.  First, we can emulate
 * the aggressive declaration (which is better for data and for speed) by
 * looking at the first ohit and seeing when we're a segment length away from
 * it, and then scanning it then and there to find something we can use.  Then
 * we can just put it into the offset_seg_map and we can avoid using the
 * hash_map later on.
 *
 * Probably a lot of other things.
 */
void
XCodecEncoder::encode(Buffer *output, Buffer *input)
{
	if (input->length() < XCODEC_SEGMENT_LENGTH) {
		output->append(input);
		input->clear();
		return;
	}

	XCHash<XCODEC_SEGMENT_LENGTH> xcodec_hash;
	std::deque<std::pair<unsigned, uint64_t> > offset_hash_map;
	std::deque<std::pair<unsigned, BufferSegment *> > offset_seg_map;
	Buffer outq;
	unsigned o = 0;
	unsigned base = 0;

	while (!input->empty()) {
		BufferSegment *seg;
		input->moveout(&seg);

		outq.append(seg);

		const uint8_t *p;
		for (p = seg->data(); p < seg->end(); p++) {
			if (++o < base)
				continue;

			xcodec_hash.roll(*p);

			if (o - base < XCODEC_SEGMENT_LENGTH)
				continue;

			unsigned start = o - XCODEC_SEGMENT_LENGTH;
			uint64_t hash = xcodec_hash.mix();

			BufferSegment *oseg;
			oseg = database_->lookup(hash);
			if (oseg != NULL) {
				/*
				 * This segment already exists.  If it's
				 * identical to this chunk of data, then that's
				 * positively fantastic.
				 */
				uint8_t data[XCODEC_SEGMENT_LENGTH];
				outq.copyout(data, start, sizeof data);

				if (!oseg->match(data, sizeof data)) {
					DEBUG(log_) << "Collision in first pass.";
					continue;
				}

				/*
				 * The segment was identical, we can use it.
				 * We're giving our reference to the offset-seg
				 * map.
				 */
				std::pair<unsigned, BufferSegment *> osp;
				osp.first = start;
				osp.second = oseg;
				offset_seg_map.push_back(osp);

				/* Do not hash any data until after us.  */
				base = o;
			}

			/*
			 * No collision, remember this for later.
			 */
			std::pair<unsigned, uint64_t> ohp;
			ohp.first = start;
			ohp.second = hash;
			offset_hash_map.push_back(ohp);
		}
		seg->unref();
	}

	/*
	 * Now compile the offset-hash map into child data.
	 */

	std::deque<std::pair<unsigned, uint64_t> >::iterator ohit;
	BufferSegment *seg;
	unsigned soff = 0;
	while ((ohit = offset_hash_map.begin()) != offset_hash_map.end()) {
		unsigned start = ohit->first;
		uint64_t hash = ohit->second;
		unsigned end = start + XCODEC_SEGMENT_LENGTH;

		/*
		 * We only get one bite at the apple.
		 */
		offset_hash_map.erase(ohit);

		std::deque<std::pair<unsigned, BufferSegment *> >::iterator osit = offset_seg_map.begin();
		Data slice;

		/*
		 * If this offset-hash corresponds to this offset-segment, use
		 * it!
		 */
		if (osit != offset_seg_map.end()) {
			if (start == osit->first) {
				slice.hash_ = hash;
				/* The slice holds our reference.  */
				slice.seg_ = osit->second;

				/*
				 * Dispose of this entry.
				 */
				offset_seg_map.erase(osit);
			} else if (start < osit->first && end > osit->first) {
				/*
				 * This hash would overlap with a
				 * offset-segment.  Skip it.
				 */
				continue;
			} else {
				/*
				 * There is an offset-segment in our distant
				 * future, we can try this hash for now.
				 */
			}
		} else {
			/*
			 * There is no offset-segment after this, so we can just
			 * use this hash gleefully.
			 */
		}

		/*
		 * We have not yet set a seg_ in this Data, so it's time for us
		 * to declare this segment.
		 */
		if (slice.seg_ == NULL) {
			uint8_t data[XCODEC_SEGMENT_LENGTH];
			outq.copyout(data, start - soff, sizeof data);

			/*
			 * We can't assume that this isn't in the database.
			 * Since we're declaring things all the time in this
			 * stream, we may have introduced hits and collisions.
			 * So we, sadly, have to go back to the well.
			 */
			seg = database_->lookup(hash);
			if (seg != NULL) {
				if (!seg->match(data, sizeof data)) {
					seg->unref();
					DEBUG(log_) << "Collision in second pass.";
					continue;
				}

				/*
				 * A hit!  Well, that's fantastic.
				 */
				slice.hash_ = hash;
				/* The slice holds our reference.  */
				slice.seg_ = seg;
			} else {
				/*
				 * No hit is fantastic, too -- go ahead and
				 * declare this hash.
				 */
				seg = new BufferSegment();
				seg->append(data, sizeof data);

				database_->enter(hash, seg);

				slice.hash_ = hash;
				/* The slice holds our reference.  */
				slice.seg_ = seg;

				output->append(XCODEC_DECLARE_CHAR);
				uint64_t lehash = LittleEndian::encode(slice.hash_);
				output->append((const uint8_t *)&lehash, sizeof lehash);
				output->append(slice.seg_);

				backref_.declare(slice.hash_, slice.seg_);
			}

			/*
			 * Skip any successive overlapping hashes.
			 */
			while ((ohit = offset_hash_map.begin()) != offset_hash_map.end()) {
				if (ohit->first >= end)
					break;
				offset_hash_map.erase(ohit);
			}
		} else {
			/*
			 * There should not be any successive overlapping hashes
			 * if we found a hit in the first pass.  XXX We should
			 * ASSERT that this looks like we'd expect.
			 */
		}

		ASSERT(slice.seg_ != NULL);

		/*
		 * Copy out any prefixing data.
		 */
		if (soff != start) {
			outq.moveout(&slice.prefix_, 0, start - soff);
			soff = start;

			slice.prefix_.escape(XCODEC_ESCAPE_CHAR, xcodec_special_p());
			output->append(slice.prefix_);
			slice.prefix_.clear();
		}

		/*
		 * And skip this segment.
		 */
		outq.skip(XCODEC_SEGMENT_LENGTH); 
		soff = end;

		/*
		 * And output a reference.
		 */
		uint8_t b;
		if (backref_.present(slice.hash_, &b)) {
			output->append(XCODEC_BACKREF_CHAR);
			output->append(b);
		} else {
			output->append(XCODEC_HASHREF_CHAR);
			uint64_t lehash = LittleEndian::encode(slice.hash_);
			output->append((const uint8_t *)&lehash, sizeof lehash);

			backref_.declare(slice.hash_, slice.seg_);
		}
	}

	/*
	 * The segment map should be empty, too.  It should only have entries
	 * that correspond to offset-hash entries.
	 */
	ASSERT(offset_seg_map.empty());

	if (!outq.empty()) {
		Buffer suffix(outq);
		outq.clear();

		suffix.escape(XCODEC_ESCAPE_CHAR, xcodec_special_p());

		output->append(suffix);
		outq.clear();
	}

	ASSERT(outq.empty());
	ASSERT(input->empty());
}
