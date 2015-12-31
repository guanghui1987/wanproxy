/*
 * Copyright (c) 2010-2013 Juli Mallett. All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 *
 * THIS SOFTWARE IS PROVIDED BY AUTHOR AND CONTRIBUTORS ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED.  IN NO EVENT SHALL AUTHOR OR CONTRIBUTORS BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 */

#ifndef	EVENT_CONDITION_H
#define	EVENT_CONDITION_H

class Condition {
protected:
	Condition(void)
	{ }

	virtual ~Condition()
	{ }

public:
	virtual void signal(void) = 0;
	virtual Action *wait(SimpleCallback *) = 0;
};

class ConditionVariable : public Condition {
	Action *wait_action_;
	SimpleCallback *wait_callback_;
public:
	ConditionVariable(void)
	: wait_action_(NULL),
	  wait_callback_(NULL)
	{ }

	~ConditionVariable()
	{
		ASSERT_NULL("/condition/variable", wait_action_);
		ASSERT_NULL("/condition/variable", wait_callback_);
	}

	void signal(void)
	{
		if (wait_callback_ == NULL)
			return;
		ASSERT_NULL("/condition/variable", wait_action_);
		wait_action_ = wait_callback_->schedule();
		wait_callback_ = NULL;
	}

	Action *wait(SimpleCallback *cb)
	{
		ASSERT_NULL("/condition/variable", wait_action_);
		ASSERT_NULL("/condition/variable", wait_callback_);

		wait_callback_ = cb;

		return (cancellation(this, &ConditionVariable::wait_cancel));
	}

private:
	void wait_cancel(void)
	{
		if (wait_callback_ != NULL) {
			ASSERT_NULL("/condition/variable", wait_action_);

			delete wait_callback_;
			wait_callback_ = NULL;
		} else {
			ASSERT_NON_NULL("/condition/variable", wait_action_);

			wait_action_->cancel();
			wait_action_ = NULL;
		}
	}
};

#endif /* !EVENT_CONDITION_H */
