#ifndef	SSH_SSH_KEY_EXCHANGE_H
#define	SSH_SSH_KEY_EXCHANGE_H

class Buffer;

namespace SSH {
	struct Session;
	class TransportPipe;

	class KeyExchange {
		std::string name_;
	protected:
		KeyExchange(const std::string& xname)
		: name_(xname)
		{ }

	public:
		virtual ~KeyExchange()
		{ }

		std::string name(void) const
		{
			return (name_);
		}

		virtual KeyExchange *clone(void) const = 0;

		virtual bool hash(Buffer *, const Buffer *) const = 0;

		virtual bool input(TransportPipe *, Buffer *) = 0;

		static KeyExchange *method(Session *);
	};
}

#endif /* !SSH_SSH_KEY_EXCHANGE_H */
