// This file is part of playd.
// playd is licenced under the MIT license: see LICENSE.txt.

/**
 * @file
 * Declaration of the Tokeniser class.
 * @see io/tokeniser.cpp
 */

#ifndef PS_TOKENISER_HPP
#define PS_TOKENISER_HPP

#include "../cmd.hpp"
#include "io_response.hpp"

/**
 * A string tokeniser.
 *
 * A Tokeniser is fed chunks of incoming data from the IO system, and emits any
 * fully-formed command lines it encounters to the command handler.
 *
 * @see CommandHandler
 * @see IoReactor
 */
class Tokeniser {
public:
	/// A single tokenised line.
	typedef std::vector<std::string> Line;

	/// A vector of tokenised lines.
	typedef std::vector<Line> Lines;

	/// Constructs a new Tokeniser.
	Tokeniser();

	/**
	 * Feeds a string into a Tokeniser.
	 * @param raw_string Const reference to the raw string to feed.  The
	 *   string need not contain complete lines.
	 * @return The vector of lines that have been successfully tokenised in
	 *   this tokenising pass.  This vector may be empty.
	 */
	Lines Feed(const std::string &raw_string);

private:
	/// Enumeration of quotation types.
	enum class QuoteType : std::uint8_t {
		NONE,   ///< Not currently in a quote pair.
		SINGLE, ///< In single quotes ('').
		DOUBLE  ///< In double quotes ("").
	};

	/// Whether the next character is to be interpreted as an escape code.
	/// This usually gets set to true when a backslash is detected.
	bool escape_next_character;

	/// The type of quotation currently being used in this Tokeniser.
	QuoteType quote_type;

	/// The current vector of completed, tokenised words.
	Line words;

	/// The current vector of completed, tokenised lines.
	/// This is cleared at the end of every Tokeniser::Feed. 
	Lines ready_lines;

	/// The current, incomplete word to which new characters should be
	/// added.
	std::string current_word;

	/// Finishes the current word and sends the line to the CommandHandler.
	void Emit();

	/// Finishes the current word, adding it to the tokenised line.
	void EndWord();

	/**
	 * Pushes a raw character onto the end of the current word.
	 *
	 * This also clears the escape_next_character flag.
	 *
	 * @param c The character to push onto the current word.
	 */
	void Push(unsigned char c);
};

#endif // PS_TOKENISER_HPP
