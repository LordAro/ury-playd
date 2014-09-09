// This file is part of playd.
// playd is licenced under the MIT license: see LICENSE.txt.

/**
 * @file
 * Declaration of classes pertaining to responses to the client.
 * @see io/io_response.cpp
 */

#ifndef PS_IO_RESPONSE_HPP
#define PS_IO_RESPONSE_HPP

#include <map>
#include <ostream>
#include <string>

#include "../errors.hpp"

/**
 * Four-character response codes.
 * @note If you're adding new responses here, update RESPONSES.
 * @see RESPONSES
 */
enum class ResponseCode {
	OKAY,     ///< Request was valid and produced an answer.
	WHAT,     ///< Request was invalid/user error.
	FAIL,     ///< Error, pointing blame at environment.
	OHAI,     ///< Server starting up.
	STATE,    ///< Server changing state.
	TIME,     ///< Server sending current song time,
	FILE,     ///< The loaded file just changed.
	FEATURES, ///< Server sending feature list.
	END       ///< The loaded file just ended on its own.
};

/**
 * A map from ResponseCode codes to their string equivalents.
 * @see ResponseCode
 */
extern const std::string RESPONSES[];

/**
 * Abstract class for anything that can be sent a response.
 * Usually the responses come from a ResponseSource, but anything may send a
 * ResponseSink a response.
 * @see ResponseSource
 */
class ResponseSink {
public:
	/**
	 * Outputs a response.
	 * @param code The response code to emit.
	 * @param message The response message.
	 */
	void Respond(ResponseCode code, const std::string &message) const;

	/**
	 * Emits an error as a response.
	 * @param error The error to convert to a response.
	 */
	void RespondWithError(const Error &error) const;

protected:
	/**
	 * Outputs a raw response string.
	 * @param string The response string, of the form "CODE message".
	 */
	virtual void RespondRaw(const std::string &string) const = 0;
};

/**
 * Abstract helper class for sources of responses.
 *
 * A ResponseSource can both 'push' responses to a registered ResponseSink and
 * be 'polled' from outside to dump its current response to an external
 * ResponseSink.  For example, PlayerPosition 'pushes' its position every few
 * milliseconds to the outside world, to keep the client aware of the time,
 * but is also 'polled' on a new client connection so that the client
 * immediately gets the current position on connect.
 *
 * @see ResponseSink
 */
class ResponseSource {
public:
	/**
	 * Emits a response to a given ResponseSink.
	 * @param sink The ResponseSink to which this ResponseSource's current
	 * response should be emitted.
	 */
	virtual void Emit(ResponseSink &sink) const = 0;

	/**
	 * Registers a ResponseSink with this ResponseSource.
	 * The ResponseSource will periodically send a response to the given
	 * ResponseSink.
	 * @param sink The ResponseSink to register.
	 */
	void SetResponseSink(ResponseSink &sink);

protected:
	/**
	 * Calls an Emit on the registered ResponseSink.
	 * If there is no registered ResponseSink, the response is dropped.
	 */
	void Push() const;

private:
	/**
	 * A ResponseSink to which 'push' responses are emitted.
	 * If the ResponseSink is not present, responses are not emitted.
	 */
	ResponseSink *push_sink;
};

#endif // PS_IO_RESPONSE_HPP
