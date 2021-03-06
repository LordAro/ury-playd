// This file is part of playd.
// playd is licensed under the MIT licence: see LICENSE.txt.

/**
 * @file
 * Implementation of the playd Error exception set.
 * @see errors.hpp
 */

#include "errors.hpp"

Error::Error(const std::string &msg) : message(msg)
{
}

const std::string &Error::Message() const
{
	return this->message;
}
