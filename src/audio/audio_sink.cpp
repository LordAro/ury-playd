// This file is part of playd.
// playd is licensed under the MIT licence: see LICENSE.txt.

/**
 * @file
 * Implementation of the AudioSink class.
 * @see audio/audio_sink.hpp
 */

#include <algorithm>
#include <cassert>
#include <climits>
#include <cstring>
#include <memory>
#include <string>

#include "SDL.h"

#include "../errors.hpp"
#include "../messages.h"
#include "audio_sink.hpp"
#include "audio_source.hpp"
#include "ringbuffer.hpp"
#include "sample_formats.hpp"

//
// AudioSink
//

Audio::State AudioSink::State()
{
	return Audio::State::NONE;
}

//
// SdlAudioSink
//

const size_t SdlAudioSink::RINGBUF_POWER = 16;

/**
 * The callback used by SDL_Audio.
 * Trampolines back into vsink, which must point to an SdlAudioSink.
 */
static void SDLCallback(void *vsink, std::uint8_t *data, int len)
{
	assert(vsink != nullptr);
	auto sink = static_cast<SdlAudioSink *>(vsink);
	sink->Callback(data, len);
}

/* static */ std::unique_ptr<AudioSink> SdlAudioSink::Build(
        const AudioSource &source, int device_id)
{
	return std::unique_ptr<AudioSink>(new SdlAudioSink(source, device_id));
}

SdlAudioSink::SdlAudioSink(const AudioSource &source, int device_id)
    : bytes_per_sample(source.BytesPerSample()),
      ring_buf(RINGBUF_POWER, source.BytesPerSample()),
      position_sample_count(0),
      source_out(false),
      state(Audio::State::STOPPED)
{
	const char *name = SDL_GetAudioDeviceName(device_id, 0);
	if (name == nullptr) {
		throw ConfigError(std::string("invalid device id: ") +
		                  std::to_string(device_id));
	}

	SDL_AudioSpec want;
	SDL_zero(want);
	want.freq = source.SampleRate();
	want.format = SDLFormat(source.OutputSampleFormat());
	want.channels = source.ChannelCount();
	want.callback = &SDLCallback;
	want.userdata = (void *)this;

	SDL_AudioSpec have;
	SDL_zero(have);

	this->device = SDL_OpenAudioDevice(name, 0, &want, &have, 0);
	if (this->device == 0) {
		throw ConfigError(std::string("couldn't open device: ") +
		                  SDL_GetError());
	}
}

SdlAudioSink::~SdlAudioSink()
{
	if (this->device == 0) return;

	// Silence any currently playing audio.
	SDL_PauseAudioDevice(this->device, SDL_TRUE);
	SDL_CloseAudioDevice(this->device);
}

/* static */ void SdlAudioSink::InitLibrary()
{
	if (SDL_Init(SDL_INIT_AUDIO) != 0) {
		throw ConfigError(std::string("could not initialise SDL: ") +
		                  SDL_GetError());
	}
}

/* static */ void SdlAudioSink::CleanupLibrary()
{
	SDL_Quit();
}

void SdlAudioSink::Start()
{
	if (this->state != Audio::State::STOPPED) return;

	SDL_PauseAudioDevice(this->device, 0);
	this->state = Audio::State::PLAYING;
}

void SdlAudioSink::Stop()
{
	if (this->state == Audio::State::STOPPED) return;

	SDL_PauseAudioDevice(this->device, 1);
	this->state = Audio::State::STOPPED;
}

Audio::State SdlAudioSink::State()
{
	return this->state;
}

void SdlAudioSink::SourceOut()
{
	// The sink should only be out if the source is.
	assert(this->source_out || this->state != Audio::State::AT_END);

	this->source_out = true;
}

std::uint64_t SdlAudioSink::Position()
{
	return this->position_sample_count;
}

void SdlAudioSink::SetPosition(std::uint64_t samples)
{
	this->position_sample_count = samples;

	// We might have been at the end of the file previously.
	// If so, we might not be now, so clear the out flags.
	this->source_out = false;
	if (this->state == Audio::State::AT_END) {
		this->state = Audio::State::STOPPED;
		this->Stop();
	}

	// The ringbuf will have been full of samples from the old
	// position, so we need to get rid of them.
	this->ring_buf.Flush();
}

void SdlAudioSink::Transfer(AudioSink::TransferIterator &start,
                            const AudioSink::TransferIterator &end)
{
	assert(start <= end);

	// No point transferring 0 bytes.
	if (start == end) return;

	unsigned long bytes = std::distance(start, end);
	// There should be a whole number of samples being transferred.
	assert(bytes % bytes_per_sample == 0);
	assert(0 < bytes);

	auto samples = bytes / this->bytes_per_sample;

	// Only transfer as many samples as the ring buffer can take.
	// Don't bother trying to write 0 samples!
	auto count = std::min(samples, this->ring_buf.WriteCapacity());
	if (count == 0) return;

	auto start_ptr = reinterpret_cast<char *>(&*start);
	unsigned long written_count = this->ring_buf.Write(start_ptr, count);
	// Since we never write more than the ring buffer can take, the written
	// count should equal the requested written count.
	assert(written_count == count);

	start += (written_count * this->bytes_per_sample);
	assert(start <= end);
}

void SdlAudioSink::Callback(std::uint8_t *out, int nbytes)
{
	assert(out != nullptr);

	assert(0 <= nbytes);
	unsigned long lnbytes = static_cast<unsigned long>(nbytes);

	// Make sure anything not filled up with sound later is set to silence.
	// This is slightly inefficient (two writes to sound-filled regions
	// instead of one), but more elegant in failure cases.
	memset(out, 0, lnbytes);

	// If we're not supposed to be playing, don't play anything.
	if (this->state != Audio::State::PLAYING) return;

	// Let's find out how many samples are available in total to give SDL.
	//
	// Note: Since we run concurrently with the decoder, which is also
	// trying to modify the read capacity of the ringbuf (by adding
	// things), this technically causes a race condition when we try to
	// read `avail_samples` number of samples later.  Not to fear: the
	// actual read capacity can only be greater than or equal to
	// `avail_samples`, as this is the only place where we can *decrease*
	// it.
	auto avail_samples = this->ring_buf.ReadCapacity();

	// Have we run out of things to feed?
	if (avail_samples == 0) {
		// Is this a temporary condition, or have we genuinely played
		// out all we can?  If the latter, we're now out too.
		if (this->source_out) this->state = Audio::State::AT_END;

		// Don't even bother reading from the ring buffer.
		return;
	}

	// How many samples do we want to pull out of the ring buffer?
	auto req_samples = lnbytes / this->bytes_per_sample;

	// How many can we pull out?  Send this amount to SDL.
	auto samples = std::min(req_samples, avail_samples);
	auto read_samples =
	        this->ring_buf.Read(reinterpret_cast<char *>(out), samples);
	this->position_sample_count += read_samples;
}

/// Mappings from SampleFormats to their equivalent SDL_AudioFormats.
static const std::map<SampleFormat, SDL_AudioFormat> sdl_from_sf = {
        {SampleFormat::PACKED_UNSIGNED_INT_8, AUDIO_U8},
        {SampleFormat::PACKED_SIGNED_INT_8, AUDIO_S8},
        {SampleFormat::PACKED_SIGNED_INT_16, AUDIO_S16},
        {SampleFormat::PACKED_SIGNED_INT_32, AUDIO_S32},
        {SampleFormat::PACKED_FLOAT_32, AUDIO_F32}};

/* static */ SDL_AudioFormat SdlAudioSink::SDLFormat(SampleFormat fmt)
{
	try {
		return sdl_from_sf.at(fmt);
	} catch (std::out_of_range) {
		throw FileError(MSG_DECODE_BADRATE);
	}
}

/* static */ std::vector<std::pair<int, std::string>> SdlAudioSink::GetDevicesInfo()
{
	std::vector<std::pair<int, std::string>> list;

	// The 0 in SDL_GetNumAudioDevices tells SDL we want playback devices.
	int is = SDL_GetNumAudioDevices(0);
	for (int i = 0; i < is; i++) {
		const char *n = SDL_GetAudioDeviceName(i, 0);
		if (n == nullptr) continue;

		list.emplace_back(i, std::string(n));
	}

	return list;
}

/* static */ bool SdlAudioSink::IsOutputDevice(int id)
{
	int ids = SDL_GetNumAudioDevices(0);

	// See above comment for why this is sufficient.
	return (0 <= id && id < ids);
}
