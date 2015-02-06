// This file is part of playd.
// playd is licensed under the MIT licence: see LICENSE.txt.

/**
 * @file
 * Declaration of the Audio class.
 * @see audio/audio.cpp
 */

#ifndef PLAYD_AUDIO_HPP
#define PLAYD_AUDIO_HPP

#include <cstdint>
#include <memory>
#include <string>
#include <utility>
#include <vector>

#include "../response.hpp"
#include "audio_source.hpp"

class AudioSink;

/**
 * An audio item.
 *
 * Audio abstractly represents an audio item that can be played, stopped,
 * and queried for its position and path (or equivalent).
 *
 * Audio is a virtual interface implemented concretely by PipeAudio, and
 * also by mock implementations for testing purposes.
 *
 * @see PipeAudio
 */
class Audio
{
public:
	/**
	 * Enumeration of possible states for this Audio.
	 * @see Update
	 */
	enum class State : uint8_t {
		NONE,    ///< There is no Audio.
		STOPPED, ///< The Audio has been stopped, or not yet played.
		PLAYING, ///< The Audio is currently playing.
		AT_END,  ///< The Audio has ended and can't play without a seek.
	};

	/// Virtual, empty destructor for Audio.
	virtual ~Audio() = default;

	//
	// Control interface
	//

	/**
	 * Sets whether this Audio should be playing or not.
	 * @param playing True for playing; false for stopped.
	 */
	virtual void SetPlaying(bool playing) = 0;

	/**
	 * Attempts to seek to the given position.
	 * @param position The position to seek to, in microseconds.
	 * @see Position
	 */
	virtual void Seek(std::uint64_t position) = 0;

	/**
	 * Performs an update cycle on this Audio.
	 *
	 * Depending on the Audio implementation, this may do actions such as
	 * performing a decoding round, checking for end-of-file, transferring
	 * frames, and so on.
	 *
	 * @return The state of the Audio after updating.
	 * @see State
	 */
	virtual State Update() = 0;

	//
	// Property access
	//

	/**
	 * Emits the requested response.
	 *
	 * @param response The response to emit, if possible.
	 * @param sink The ResponseSink to which the response shall be sent.
	 *   May be nullptr, in which case Emit should be a no-operation.
	 * @param id The ID of the connection to which the ResponseSink should
	 *   route the response.  May be 0 (the default), for all (broadcast).
	 */
	virtual void Emit(Response::Code code, const ResponseSink *sink, size_t id=0) = 0;

	/**
	 * This Audio's current position.
	 *
	 * As this may be executing whilst the playing callback is running,
	 * do not expect it to be highly accurate.
	 *
	 * @return The current position, in microseconds.
	 * @see Seek
	 */
	virtual std::uint64_t Position() const = 0;
};

/**
 * A dummy Audio implementation representing a lack of file.
 *
 * NoAudio throws exceptions if any attempt is made to change, start, or stop
 * the audio, and returns Audio::State::NONE during any attempt to Update.
 * If asked to emit the audio file, NoAudio does nothing.
 *
 * @see Audio
 */
class NoAudio : public Audio
{
public:
	Audio::State Update() override;
	void Emit(Response::Code code, const ResponseSink *sink, size_t id) override;

	// The following all raise an exception:

	void SetPlaying(bool playing) override;
	void Seek(std::uint64_t position) override;
	std::uint64_t Position() const override;
};

/**
 * A concrete implementation of Audio as a 'pipe'.
 *
 * PipeAudio is comprised of a 'source', which decodes frames from a
 * file, and a 'sink', which plays out the decoded frames.  Updating
 * consists of shifting frames from the source to the sink.
 *
 * @see Audio
 * @see AudioSink
 * @see AudioSource
 */
class PipeAudio : public Audio
{
public:
	/**
	 * Constructs a PipeAudio from a source and a sink.
	 * @param src The source of decoded audio frames.
	 * @param sink The target of decoded audio frames.
	 * @see AudioSystem::Load
	 */
	PipeAudio(std::unique_ptr<AudioSource> &&src,
	          std::unique_ptr<AudioSink> &&sink);

	void SetPlaying(bool playing) override;
	void Seek(std::uint64_t position) override;
	Audio::State Update() override;

	void Emit(Response::Code code, const ResponseSink *sink, size_t id) override;
	std::uint64_t Position() const override;

private:
	/// The source of audio data.
	std::unique_ptr<AudioSource> src;

	/// The sink to which audio data is sent.
	std::unique_ptr<AudioSink> sink;

	/// The current decoded frame.
	AudioSource::DecodeVector frame;

	/// The current position in the current decoded frame.
	AudioSource::DecodeVector::iterator frame_iterator;

	/**
	 * A map of response sink pointers to their last emitted position, in
	 * seconds.
	 *
	 * The pointers are not actually used in any way other than as a
	 * convenient identifier for each individual ResponseSink.  As such,
	 * the ResponseSinks are not owned by this object.
	 *
	 * @todo (CaptainHayashi) This could cause memory leaks if not emptied
	 *   regularly--need to check.
	 */
	std::map<const ResponseSink *, std::uint64_t> last_times;

	/// Clears the current frame and its iterator.
	void ClearFrame();

	/**
	 * Decodes a new frame, if the current frame is empty.
	 * @return True if more frames are available to decode; false
	 *   otherwise.
	 */
	bool DecodeIfFrameEmpty();

	/**
	 * Returns whether the current frame has been finished.
	 * If this is true, then either the frame is empty, or all of the
	 * samples in the frame have been fed to the ringbuffer.
	 * @return True if the frame is finished; false otherwise.
	 */
	bool FrameFinished() const;

	/// Transfers as much of the current frame as possible to the sink.
	void TransferFrame();

	/**
	 * Determines whether we can broadcast a TIME response.
	 *
	 * To prevent spewing massive amounts of TIME responses, we only send a
	 * broadcast if the number of seconds has changed since the last
	 * time CanAnnounceTime() was called for the given sink.
	 *
	 * This is *not* idempotent.  A CanAnnounceTime(x) less than one second
	 * before a CanAnnounceTime(x) will _always_ be false.
	 *
	 * @param micros The value of the TIME response, in microseconds.
	 * @param sink The ResponseSink to which a TIME will be broadcast if
	 *   this returns true.
	 * @return Whether it is polite to send TIME to the given sink.
	 */
	bool CanAnnounceTime(std::uint64_t micros, const ResponseSink *sink);
};

#endif // PLAYD_AUDIO_HPP
