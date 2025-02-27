// Copyright 2019 Citra Emulator Project
// Licensed under GPLv2 or any later version
// Refer to the license.txt file included.

#include <fdk-aac/aacdecoder_lib.h>
#include "audio_core/hle/fdk_decoder.h"

namespace AudioCore::HLE {

class FDKDecoder::Impl {
public:
    explicit Impl(Memory::MemorySystem& memory);
    ~Impl();
    std::optional<BinaryMessage> ProcessRequest(const BinaryMessage& request);
    bool IsValid() const {
        return decoder != nullptr;
    }

private:
    std::optional<BinaryMessage> Initalize(const BinaryMessage& request);

    std::optional<BinaryMessage> Decode(const BinaryMessage& request);

    void Clear();

    Memory::MemorySystem& memory;

    HANDLE_AACDECODER decoder = nullptr;
};

FDKDecoder::Impl::Impl(Memory::MemorySystem& memory) : memory(memory) {
    // allocate an array of LIB_INFO structures
    // if we don't pre-fill the whole segment with zeros, when we call `aacDecoder_GetLibInfo`
    // it will segfault, upon investigation, there is some code in fdk_aac depends on your initial
    // values in this array
    LIB_INFO decoder_info[FDK_MODULE_LAST] = {};
    // get library information and fill the struct
    if (aacDecoder_GetLibInfo(decoder_info) != 0) {
        LOG_ERROR(Audio_DSP, "Failed to retrieve fdk_aac library information!");
        return;
    }

    LOG_INFO(Audio_DSP, "Using fdk_aac version {} (build date: {})", decoder_info[0].versionStr,
             decoder_info[0].build_date);

    // choose the input format when initializing: 1 layer of ADTS
    decoder = aacDecoder_Open(TRANSPORT_TYPE::TT_MP4_ADTS, 1);
    // set maximum output channel to two (stereo)
    // if the input samples have more channels, fdk_aac will perform a downmix
    AAC_DECODER_ERROR ret = aacDecoder_SetParam(decoder, AAC_PCM_MAX_OUTPUT_CHANNELS, 2);
    if (ret != AAC_DEC_OK) {
        // unable to set this parameter reflects the decoder implementation might be broken
        // we'd better shuts down everything
        aacDecoder_Close(decoder);
        decoder = nullptr;
        LOG_ERROR(Audio_DSP, "Unable to set downmix parameter: {}", ret);
        return;
    }
}

std::optional<BinaryMessage> FDKDecoder::Impl::Initalize(const BinaryMessage& request) {
    BinaryMessage response = request;
    response.header.result = ResultStatus::Success;

    if (decoder) {
        LOG_INFO(Audio_DSP, "FDK Decoder initialized");
        Clear();
    } else {
        LOG_ERROR(Audio_DSP, "Decoder not initialized");
    }

    return response;
}

FDKDecoder::Impl::~Impl() {
    if (decoder)
        aacDecoder_Close(decoder);
}

void FDKDecoder::Impl::Clear() {
    s16 decoder_output[8192];
    // flush and re-sync the decoder, discarding the internal buffer
    // we actually don't care if this succeeds or not
    // FLUSH - flush internal buffer
    // INTR - treat the current internal buffer as discontinuous
    // CONCEAL - try to interpolate and smooth out the samples
    if (decoder)
        aacDecoder_DecodeFrame(decoder, decoder_output, 8192,
                               AACDEC_FLUSH & AACDEC_INTR & AACDEC_CONCEAL);
}

std::optional<BinaryMessage> FDKDecoder::Impl::ProcessRequest(const BinaryMessage& request) {
    if (request.header.codec != DecoderCodec::DecodeAAC) {
        LOG_ERROR(Audio_DSP, "FDK AAC Decoder cannot handle such codec: {}",
                  static_cast<u16>(request.header.codec));
        return {};
    }

    switch (request.header.cmd) {
    case DecoderCommand::Init: {
        return Initalize(request);
    }
    case DecoderCommand::EncodeDecode: {
        return Decode(request);
    }
    case DecoderCommand::Unknown: {
        BinaryMessage response = request;
        response.header.result = 0x0;
        return response;
    }
    default:
        LOG_ERROR(Audio_DSP, "Got unknown binary request: {}",
                  static_cast<u16>(request.header.cmd));
        return {};
    }
}

std::optional<BinaryMessage> FDKDecoder::Impl::Decode(const BinaryMessage& request) {
    BinaryMessages response;
    response.header.codec = request.header.codec;
    response.header.cmd = request.header.cmd;
    response.decode_aac_response.size = request.decode_aac_request.size;

    if (!decoder) {
        LOG_DEBUG(Audio_DSP, "Decoder not initalized");
        // This is a hack to continue games that are not compiled with the aac codec
        response.decode_aac_response.num_channels = 2;
        response.decode_aac_response.num_samples = 1024;
        return response;
    }

    if (request.decode_aac_request.src_addr < Memory::FCRAM_PADDR ||
        request.decode_aac_request.src_addr + request.decode_aac_request.size >
            Memory::FCRAM_PADDR + Memory::FCRAM_SIZE) {
        LOG_ERROR(Audio_DSP, "Got out of bounds src_addr {:08x}",
                  request.decode_aac_request.src_addr);
        return {};
    }
    u8* data = memory.GetFCRAMPointer(request.decode_aac_request.src_addr - Memory::FCRAM_PADDR);

    std::array<std::vector<s16>, 2> out_streams;

    std::size_t data_size = request.decode_aac_request.size;

    // decoding loops
    AAC_DECODER_ERROR result = AAC_DEC_OK;
    // Up to 2048 samples, up to 2 channels each
    s16 decoder_output[4096];
    // note that we don't free this pointer as it is automatically freed by fdk_aac
    CStreamInfo* stream_info;
    // how many bytes to be queued into the decoder, decrementing from the buffer size
    u32 buffer_remaining = data_size;
    // alias the data_size as an u32
    u32 input_size = data_size;

    while (buffer_remaining) {
        // queue the input buffer, fdk_aac will automatically slice out the buffer it needs
        // from the input buffer
        result = aacDecoder_Fill(decoder, &data, &input_size, &buffer_remaining);
        if (result != AAC_DEC_OK) {
            // there are some issues when queuing the input buffer
            LOG_ERROR(Audio_DSP, "Failed to enqueue the input samples");
            return std::nullopt;
        }
        // get output from decoder
        result = aacDecoder_DecodeFrame(decoder, decoder_output,
                                        sizeof(decoder_output) / sizeof(s16), 0);
        if (result == AAC_DEC_OK) {
            // get the stream information
            stream_info = aacDecoder_GetStreamInfo(decoder);
            // fill the stream information for binary response
            response.decode_aac_response.sample_rate = GetSampleRateEnum(stream_info->sampleRate);
            response.decode_aac_response.num_channels = stream_info->numChannels;
            response.decode_aac_response.num_samples = stream_info->frameSize;
            // fill the output
            // the sample size = frame_size * channel_counts
            for (int sample = 0; sample < stream_info->frameSize; sample++) {
                for (int ch = 0; ch < stream_info->numChannels; ch++) {
                    out_streams[ch].push_back(
                        decoder_output[(sample * stream_info->numChannels) + ch]);
                }
            }
        } else if (result == AAC_DEC_TRANSPORT_SYNC_ERROR) {
            // decoder has some synchronization problems, try again with new samples,
            // using old samples might trigger this error again
            continue;
        } else {
            LOG_ERROR(Audio_DSP, "Error decoding the sample: {}", result);
            return std::nullopt;
        }
    }

    // transfer the decoded buffer from vector to the FCRAM
    for (std::size_t ch = 0; ch < out_streams.size(); ch++) {
        if (!out_streams[ch].empty()) {
            auto byte_size = out_streams[ch].size() * sizeof(s16);
            auto dst = ch == 0 ? request.decode_aac_request.dst_addr_ch0
                               : request.decode_aac_request.dst_addr_ch1;
            if (dst < Memory::FCRAM_PADDR ||
                dst + byte_size > Memory::FCRAM_PADDR + Memory::FCRAM_SIZE) {
                LOG_ERROR(Audio_DSP, "Got out of bounds dst_addr_ch{} {:08x}", ch, dst);
                return {};
            }
            std::memcpy(memory.GetFCRAMPointer(dst - Memory::FCRAM_PADDR), out_streams[ch].data(),
                        byte_size);
        }
    }

    return response;
}

FDKDecoder::FDKDecoder(Memory::MemorySystem& memory) : impl(std::make_unique<Impl>(memory)) {}

FDKDecoder::~FDKDecoder() = default;

std::optional<BinaryMessage> FDKDecoder::ProcessRequest(const BinaryMessage& request) {
    return impl->ProcessRequest(request);
}

bool FDKDecoder::IsValid() const {
    return impl->IsValid();
}

} // namespace AudioCore::HLE
