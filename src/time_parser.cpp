// This file is part of playd.
// playd is licenced under the MIT license: see LICENSE.txt.

/**
 * @file
 * Implementation of the TimeParser class.
 * @see time_parser.hpp
 */

#include <algorithm>
#include <sstream>
#include <stdexcept>

#include "errors.hpp"
#include "messages.h"
#include "time_parser.hpp"

TimeParser::MicrosecondPosition TimeParser::Parse(const std::string &time_str) const
{
	auto seek = Split(time_str);
	std::string unit = seek.first;
	MicrosecondPosition num_units = seek.second;

	return static_cast<MicrosecondPosition>(UnitMultiplier(unit)) * num_units;
}

TimeParser::Multiplier TimeParser::UnitMultiplier(const std::string &unit) const
{
	// Default to microseconds if no unit is given.
	if (unit.empty()) return Multiplier::MICROSECONDS;

	if (unit == "us") return Multiplier::MICROSECONDS;
	if (unit == "ms") return Multiplier::MILLISECONDS;
	if (unit == "s") return Multiplier::SECONDS;
	if (unit == "m") return Multiplier::MINUTES;
	if (unit == "h") return Multiplier::HOURS;

	throw std::out_of_range(unit);
}

bool TimeParser::IsIn(const std::string &unit, std::initializer_list<std::string> list) const
{
	return std::find(list.begin(), list.end(), unit) != list.end();
}

std::pair<std::string, TimeParser::MicrosecondPosition> TimeParser::Split(const std::string &time_str) const
{
	std::istringstream is(time_str);
	MicrosecondPosition raw_time;
	std::string rest;

	is >> raw_time;

	// Make sure we actually get a time of some form.
	if (is.fail()) throw SeekError(MSG_SEEK_FAIL);

	is >> rest;
	return std::make_pair(rest, raw_time);
}
