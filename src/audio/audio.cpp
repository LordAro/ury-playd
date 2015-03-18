// This file is part of playd.
// playd is licensed under the MIT licence: see LICENSE.txt.

/**
 * @file
 * Implementation of the PipeAudio class.
 * @see audio/pipe_audio.hpp
 */

#include <algorithm>
#include <cassert>
#include <climits>
#include <cstdint>
#include <string>

#include "../errors.hpp"
#include "../messages.h"
#include "../response.hpp"
#include "audio.hpp"
#include "audio_sink.hpp"
#include "audio_source.hpp"
#include "sample_formats.hpp"

//
// Audio
//

void Audio::Emit(Response::Code, const ResponseSink *, size_t)
{
	// By default, emit nothing.  This is an acceptable behaviour.
}

//
// NoAudio
//

Audio::State NoAudio::Update()
{
	return Audio::State::NONE;
}

void NoAudio::Emit(Response::Code code, const ResponseSink *sink, size_t id)
{
	if (sink == nullptr) return;

	if (code == Response::Code::STATE) {
		auto r = Response(Response::Code::STATE).AddArg("Ejected");
		sink->Respond(r, id);
	}
}

void NoAudio::SetPlaying(bool)
{
	throw NoAudioError(MSG_CMD_NEEDS_LOADED);
}

void NoAudio::Seek(std::uint64_t)
{
	throw NoAudioError(MSG_CMD_NEEDS_LOADED);
}

std::uint64_t NoAudio::Position() const
{
	throw NoAudioError(MSG_CMD_NEEDS_LOADED);
}

//
// PipeAudio
//

PipeAudio::PipeAudio(std::unique_ptr<AudioSource> &&src,
                     std::unique_ptr<AudioSink> &&sink)
    : src(std::move(src)), sink(std::move(sink))
{
	this->ClearFrame();
}

void PipeAudio::Emit(Response::Code code, const ResponseSink *rs, size_t id)
{
	if (rs == nullptr) return;

	assert(this->src != nullptr);
	assert(this->sink != nullptr);

	Response r(code);

	switch (code) {
		case Response::Code::STATE: {
			auto state = this->sink->State();
			auto playing = state == Audio::State::PLAYING;
			r.AddArg(playing ? "Playing" : "Stopped");
		} break;
		case Response::Code::FILE: {
			r.AddArg(this->src->Path());
		} break;
		case Response::Code::TIME: {
			std::uint64_t micros = this->Position();

			// Always announce a broadcast.
			// Only announce unicasts if CanAnnounceTime(...).
			bool can = 0 < id || this->CanAnnounceTime(micros, rs);
			if (!can) return;
			r.AddArg(std::to_string(micros));
		} break;
		default:
			return;
	}

	rs->Respond(r, id);
}

void PipeAudio::SetPlaying(bool playing)
{
	assert(this->sink != nullptr);

	if (playing) {
		this->sink->Start();
	} else {
		this->sink->Stop();
	}
}

std::uint64_t PipeAudio::Position() const
{
	assert(this->sink != nullptr);
	assert(this->src != nullptr);

	return this->src->MicrosFromSamples(this->sink->Position());
}

void PipeAudio::Seek(std::uint64_t position)
{
	assert(this->sink != nullptr);
	assert(this->src != nullptr);

	auto in_samples = this->src->SamplesFromMicros(position);
	auto out_samples = this->src->Seek(in_samples);
	this->sink->SetPosition(out_samples);

	// Make sure we always announce the new position to all response sinks.
	this->last_times.clear();

	// We might still have decoded samples from the old position in
	// our frame, so clear them out.
	this->ClearFrame();
}

void PipeAudio::ClearFrame()
{
	this->frame.clear();
	this->frame_iterator = this->frame.end();
}

Audio::State PipeAudio::Update()
{
	assert(this->sink != nullptr);
	assert(this->src != nullptr);

	bool more_available = this->DecodeIfFrameEmpty();
	if (!more_available) this->sink->SourceOut();

	if (!this->FrameFinished()) this->TransferFrame();

	return this->sink->State();
}

void PipeAudio::TransferFrame()
{
	assert(!this->frame.empty());
	assert(this->sink != nullptr);
	assert(this->src != nullptr);

	this->sink->Transfer(this->frame_iterator, this->frame.end());

	// We empty the frame once we're done with it.  This
	// maintains FrameFinished(), as an empty frame is a finished one.
	if (this->FrameFinished()) {
		this->ClearFrame();
		assert(this->FrameFinished());
	}

	// The frame iterator should be somewhere between the beginning and
	// end of the frame, unless the frame was emptied.
	assert(this->frame.empty() ||
	       (this->frame.begin() <= this->frame_iterator &&
	        this->frame_iterator < this->frame.end()));
}

bool PipeAudio::DecodeIfFrameEmpty()
{
	// Either the current frame is in progress, or has been emptied.
	// AdvanceFrameIterator() establishes this assertion by emptying a
	// frame as soon as it finishes.
	assert(this->frame.empty() || !this->FrameFinished());

	// If we still have a frame, don't bother decoding yet.
	if (!this->FrameFinished()) return true;

	assert(this->src != nullptr);
	AudioSource::DecodeResult result = this->src->Decode();

	this->frame = result.second;
	this->frame_iterator = this->frame.begin();

	return result.first != AudioSource::DecodeState::END_OF_FILE;
}

bool PipeAudio::FrameFinished() const
{
	return this->frame.end() <= this->frame_iterator;
}

bool PipeAudio::CanAnnounceTime(std::uint64_t micros, const ResponseSink *sink)
{
	std::uint64_t secs = micros / 1000 / 1000;

	auto last_entry = this->last_times.find(sink);

	// We can announce if we don't have a record for this sink, or if the
	// last record was in a previous second.
	bool announce = true;
	if (last_entry != this->last_times.end()) {
		announce = last_entry->second < secs;

		// This is so as to allow the emplace below to work--it fails
		// if there's already a value under the same key.
		if (announce) this->last_times.erase(last_entry);
	}

	if (announce) this->last_times.emplace(sink, secs);

	return announce;
}
