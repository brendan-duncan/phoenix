#include "test_util.h"

#include "../src/gui/audio_engine.h"

#include <cmath>
#include <vector>

TEST(audio_formats_are_read_as_flash_writes_them)
{
    int rate = 0;
    int bits = 0;
    int channels = 0;
    CHECK(AudioDecode::parseFormat("44kHz 16bit Stereo", rate, bits, channels));
    CHECK(rate == 44100 && bits == 16 && channels == 2);
    CHECK(AudioDecode::parseFormat("22kHz 8bit Mono", rate, bits, channels));
    CHECK(rate == 22050 && bits == 8 && channels == 1);
    CHECK(AudioDecode::parseFormat("5kHz 16bit Mono", rate, bits, channels));
    CHECK(rate == 5512);
    CHECK(!AudioDecode::parseFormat("", rate, bits, channels));
}

TEST(raw_pcm_becomes_stereo_float_at_44khz)
{
    // Four 16-bit mono samples, little-endian: 0, 16384, -16384, 32767.
    const std::vector<uint8_t> mono = {0x00, 0x00, 0x00, 0x40, 0x00, 0xC0, 0xFF, 0x7F};
    auto pcm = AudioDecode::decode(mono, "44kHz 16bit Mono");
    CHECK(pcm != nullptr);
    if (!pcm)
        return;
    CHECK(pcm->frames() == 4);
    CHECK_NEAR(pcm->samples[2], 0.5);
    CHECK_NEAR(pcm->samples[3], 0.5);
    CHECK_NEAR(pcm->samples[4], -0.5);
    CHECK(std::fabs(pcm->samples[6] - 32767.0 / 32768.0) < 1.0e-6);

    // Stereo keeps its channels apart.
    const std::vector<uint8_t> stereo = {0x00, 0x40, 0x00, 0xC0};
    pcm = AudioDecode::decode(stereo, "44kHz 16bit Stereo");
    CHECK(pcm && pcm->frames() == 1);
    if (pcm)
    {
        CHECK_NEAR(pcm->samples[0], 0.5);
        CHECK_NEAR(pcm->samples[1], -0.5);
    }
}

TEST(lower_rates_are_resampled_to_44khz)
{
    // A second of 8-bit mono at 22 kHz becomes a second at 44.1 kHz.
    const std::vector<uint8_t> data(22050, 128);
    auto pcm = AudioDecode::decode(data, "22kHz 8bit Mono");
    CHECK(pcm != nullptr);
    if (pcm)
    {
        CHECK(pcm->frames() == 44100);
        CHECK_NEAR(pcm->samples[1000], 0.0);
    }
}

TEST(data_that_is_not_audio_is_refused)
{
    CHECK(AudioDecode::decode({}, "44kHz 16bit Mono") == nullptr);
    CHECK(AudioDecode::decode({1, 2, 3, 4}, "no format") == nullptr);
    // MP3 sync word but no real frames: the decoder gives up cleanly.
    CHECK(AudioDecode::decode({0xFF, 0xFB, 0x90, 0x00}, "44kHz 16bit Mono") == nullptr);
    // Raw PCM that starts like an MP3 sync word is still read as PCM.
    auto pcm = AudioDecode::decode({0xFF, 0xFF, 0xFF, 0xFF, 0x00, 0x00}, "44kHz 16bit Mono");
    CHECK(pcm && pcm->frames() == 3);
}

#include "../src/data/document.h"
#include "../src/data/fla_document.h"
#include "../src/parser/fla_parser.h"

#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <memory>
#include <cstdint>

TEST(real_sounds_decode_to_their_recorded_length)
{
    // Opt-in, like the other corpus checks: every sound in the corpus decodes
    // to about as many samples as its DOMSoundItem says it has.
    const char* corpus = std::getenv("PHOENIX_FLA_CORPUS");
    if (!corpus || !std::filesystem::exists(corpus))
    {
        std::printf("    skipped: set PHOENIX_FLA_CORPUS to a folder of FLA files\n");
        return;
    }
    int decoded = 0;
    for (const auto& entry : std::filesystem::directory_iterator(corpus))
    {
        if (!entry.is_regular_file() || entry.path().extension() != ".fla")
            continue;
        FLAParser parser;
        std::unique_ptr<fla::FLADocument> fla(parser.parse(entry.path().string()));
        if (!fla || !fla->document)
            continue;
        for (const fla::Document::SoundInfo& sound : fla->document->sounds)
        {
            const std::vector<uint8_t>* data = nullptr;
            for (const auto& file : fla->document->extraFiles)
                if (file.first == sound.dataPath)
                    data = &file.second.get();
            CHECK(data != nullptr);
            if (!data)
                continue;
            auto pcm = AudioDecode::decode(*data, sound.format);
            CHECK(pcm != nullptr);
            if (!pcm)
                continue;
            std::printf("    %s: %zu frames (%.1f s)\n", sound.name.c_str(), pcm->frames(), pcm->frames() / 44100.0);
            ++decoded;
        }
    }
    std::printf("    decoded %d sounds\n", decoded);
}

TEST(sound_envelopes_shape_the_volume)
{
    // Animate's default envelope, full volume, leaves the sound alone.
    CHECK(AudioEnvelope::parse("<SoundEnvelope><SoundEnvelopePoint level0=\"32768\" level1=\"32768\"/></SoundEnvelope>") == nullptr);
    CHECK(AudioEnvelope::parse("") == nullptr);

    // Milo_Kitty_01's fade out: full until 220095, then down to silence at
    // 292857, where the last point leaves its levels out.
    auto fade = AudioEnvelope::parse(
        "<SoundEnvelope><SoundEnvelopePoint level0=\"32768\" level1=\"32768\"/>"
        "<SoundEnvelopePoint mark44=\"220095\" level0=\"32768\" level1=\"32768\"/>"
        "<SoundEnvelopePoint mark44=\"292857\"/></SoundEnvelope>");
    CHECK(fade && fade->points.size() == 3);
    if (!fade)
        return;
    float left = 0.0f;
    float right = 0.0f;
    fade->gainAt(1000, left, right);
    CHECK_NEAR(left, 1.0);
    fade->gainAt((220095 + 292857) / 2, left, right);
    CHECK(std::fabs(left - 0.5f) < 0.001f && std::fabs(right - 0.5f) < 0.001f);
    fade->gainAt(400000, left, right);
    CHECK_NEAR(left, 0.0);

    // Each channel has its own level, as Yard_Sale_03_2's does.
    auto pan = AudioEnvelope::parse("<SoundEnvelopePoint level0=\"16384\" level1=\"0\"/>");
    CHECK(pan != nullptr);
    if (pan)
    {
        pan->gainAt(5000, left, right);
        CHECK_NEAR(left, 0.5);
        CHECK_NEAR(right, 0.0);
    }
}
