/*
 * Copyright 2017 Intel Corporation All Rights Reserved.
 *
 * The source code contained or described herein and all documents related to the
 * source code ("Material") are owned by Intel Corporation or its suppliers or
 * licensors. Title to the Material remains with Intel Corporation or its suppliers
 * and licensors. The Material contains trade secrets and proprietary and
 * confidential information of Intel or its suppliers and licensors. The Material
 * is protected by worldwide copyright and trade secret laws and treaty provisions.
 * No part of the Material may be used, copied, reproduced, modified, published,
 * uploaded, posted, transmitted, distributed, or disclosed in any way without
 * Intel's prior express written permission.
 *
 * No license under any patent, copyright, trade secret or other intellectual
 * property right is granted to or conferred upon you by disclosure or delivery of
 * the Materials, either expressly, by implication, inducement, estoppel or
 * otherwise. Any license under such intellectual property rights must be express
 * and approved by Intel in writing.
 */

#include "SVTHEVCEncoder.h"

#include <webrtc/api/video/video_frame.h>
#include <webrtc/api/video/video_frame_buffer.h>

#include <libyuv/convert.h>
#include <libyuv/planar_functions.h>
#include <libyuv/scale.h>

#include "MediaUtilities.h"

namespace woogeen_base {

DEFINE_LOGGER(SVTHEVCEncoder, "woogeen.SVTHEVCEncoder");

SVTHEVCEncoder::SVTHEVCEncoder(FrameFormat format, VideoCodecProfile profile, bool useSimulcast)
    : m_ready(false)
    , m_dest(NULL)
    , m_handle(NULL)
    , m_forceIDR(false)
    , m_frameCount(0)
    , m_enableBsDump(false)
    , m_bsDumpfp(NULL)
{
    memset(&m_encParameters, 0, sizeof(m_encParameters));
}

SVTHEVCEncoder::~SVTHEVCEncoder()
{
    if (m_ready) {
        EbDeinitEncoder(m_handle);
        EbDeinitHandle(m_handle);

        deallocateBuffers();

        if (m_bsDumpfp) {
            fclose(m_bsDumpfp);
            m_bsDumpfp = NULL;
        }

        m_dest  = NULL;
        m_ready = false;
    }
}

void SVTHEVCEncoder::initDefaultParameters()
{
    // Channel info
    m_encParameters.channelId                       = 0;
    m_encParameters.activeChannelCount              = 1;
    m_encParameters.useRoundRobinThreadAssignment   = 0;

    // GOP Structure
    m_encParameters.intraPeriodLength               = 255; //[-2 - 255]
    m_encParameters.intraRefreshType                = 2;
    m_encParameters.predStructure                   = 0; //EB_PRED_LOW_DELAY_P;
    m_encParameters.baseLayerSwitchMode             = 0;
    m_encParameters.encMode                         = 9;
    m_encParameters.hierarchicalLevels              = 3;

    m_encParameters.sourceWidth                     = 0;
    m_encParameters.sourceHeight                    = 0;
    m_encParameters.latencyMode                     = 0;

    // Interlaced Video
    m_encParameters.interlacedVideo                 = 0;

    // Quantization
    m_encParameters.qp                              = 32;
    m_encParameters.useQpFile                       = 0;

    // Deblock Filter
    m_encParameters.disableDlfFlag                  = 0;

    // SAO
    m_encParameters.enableSaoFlag                   = 1;

    // ME Tools
    m_encParameters.useDefaultMeHme                 = 1;
    m_encParameters.enableHmeFlag                   = 1;
    m_encParameters.enableHmeLevel0Flag             = 1;
    m_encParameters.enableHmeLevel1Flag             = 0;
    m_encParameters.enableHmeLevel2Flag             = 0;

    // ME Parameters
    m_encParameters.searchAreaWidth                 = 16;
    m_encParameters.searchAreaHeight                = 7;

    // HME Parameters
    m_encParameters.numberHmeSearchRegionInWidth    = 2;
    m_encParameters.numberHmeSearchRegionInHeight   = 2;
    m_encParameters.hmeLevel0TotalSearchAreaWidth   = 64;
    m_encParameters.hmeLevel0TotalSearchAreaHeight  = 25;

    m_encParameters.hmeLevel0SearchAreaInWidthArray[0]  = 32;
    m_encParameters.hmeLevel0SearchAreaInWidthArray[1]  = 32;

    m_encParameters.hmeLevel0SearchAreaInHeightArray[0] = 12;
    m_encParameters.hmeLevel0SearchAreaInHeightArray[1] = 13;

    m_encParameters.hmeLevel1SearchAreaInWidthArray[0]  = 1;
    m_encParameters.hmeLevel1SearchAreaInWidthArray[1]  = 1;

    m_encParameters.hmeLevel1SearchAreaInHeightArray[0] = 1;
    m_encParameters.hmeLevel1SearchAreaInHeightArray[1] = 1;

    m_encParameters.hmeLevel2SearchAreaInWidthArray[0]  = 1;
    m_encParameters.hmeLevel2SearchAreaInWidthArray[1]  = 1;

    m_encParameters.hmeLevel2SearchAreaInHeightArray[0] = 1;
    m_encParameters.hmeLevel2SearchAreaInHeightArray[1] = 1;

    // MD Parameters
    m_encParameters.constrainedIntra    = 0;

    // Rate Control
    m_encParameters.frameRate               = 0;
    m_encParameters.frameRateNumerator      = 0;
    m_encParameters.frameRateDenominator    = 0;
    m_encParameters.encoderBitDepth         = 8;
    m_encParameters.compressedTenBitFormat  = 0;
    m_encParameters.rateControlMode         = 1; //0 : CQP , 1 : VBR
    m_encParameters.sceneChangeDetection    = 1;
    m_encParameters.lookAheadDistance       = 0;
    m_encParameters.framesToBeEncoded       = 0;
    m_encParameters.targetBitRate           = 0;
    m_encParameters.maxQpAllowed            = 48;
    m_encParameters.minQpAllowed            = 10;
    m_encParameters.tune                    = 0;
    m_encParameters.bitRateReduction        = 1;

    // Tresholds
    m_encParameters.improveSharpness        = 1;
    m_encParameters.videoUsabilityInfo      = 0;
    m_encParameters.highDynamicRangeInput   = 0;
    m_encParameters.accessUnitDelimiter     = 0;
    m_encParameters.bufferingPeriodSEI      = 0;
    m_encParameters.pictureTimingSEI        = 0;
    m_encParameters.registeredUserDataSeiFlag   = 0;
    m_encParameters.unregisteredUserDataSeiFlag = 0;
    m_encParameters.recoveryPointSeiFlag    = 0;
    m_encParameters.enableTemporalId        = 1;
    m_encParameters.profile                 = 1;
    m_encParameters.tier                    = 0;
    m_encParameters.level                   = 0;

	// Buffer Configuration
    m_encParameters.inputOutputBufferFifoInitCount  = 0;
    m_encParameters.injectorFrameRate               = m_encParameters.frameRate << 16;
    m_encParameters.speedControlFlag                = 1;

    // ASM Type
    m_encParameters.asmType = ASM_AVX2;

    m_encParameters.codeVpsSpsPps = 1;
}

void SVTHEVCEncoder::updateParameters(uint32_t width, uint32_t height, uint32_t frameRate, uint32_t bitrateKbps, uint32_t keyFrameIntervalSeconds)
{
    //resolution
    m_encParameters.sourceWidth = width;
    m_encParameters.sourceHeight = height;

    //gop
    uint32_t intraPeriodLength              = keyFrameIntervalSeconds * frameRate;
    m_encParameters.intraPeriodLength       = (intraPeriodLength < 255) ? intraPeriodLength : 255;

    m_encParameters.frameRate               = frameRate;
    m_encParameters.injectorFrameRate       = frameRate << 16;
    m_encParameters.targetBitRate           = bitrateKbps * 1000;
}

bool SVTHEVCEncoder::canSimulcast(FrameFormat format, uint32_t width, uint32_t height)
{
    return false;
}

bool SVTHEVCEncoder::isIdle()
{
    return !m_ready;
}

int32_t SVTHEVCEncoder::generateStream(uint32_t width, uint32_t height, uint32_t frameRate, uint32_t bitrateKbps, uint32_t keyFrameIntervalSeconds, woogeen_base::FrameDestination* dest)
{
    ELOG_INFO_T("generateStream: {.width=%d, .height=%d, .frameRate=%d, .bitrateKbps=%d, .keyFrameIntervalSeconds=%d}"
            , width, height, frameRate, bitrateKbps, keyFrameIntervalSeconds);

    EB_ERRORTYPE return_error = EB_ErrorNone;

    if (m_ready) {
        ELOG_ERROR_T("Only support one stream!");
        return -1;
    }

    return_error = EbInitHandle(&m_handle, this, &m_encParameters);
    if (return_error != EB_ErrorNone) {
        ELOG_ERROR_T("InitHandle failed, ret 0x%x", return_error);
        return -1;
    }

    ELOG_DEBUG_T("SetParameter");
    initDefaultParameters();
    updateParameters(width, height, frameRate, bitrateKbps, keyFrameIntervalSeconds);

    return_error = EbH265EncSetParameter(m_handle, &m_encParameters);
    if (return_error != EB_ErrorNone) {
        ELOG_ERROR_T("SetParameter failed, ret 0x%x", return_error);

        EbDeinitHandle(m_handle);
        return -1;
    }

    ELOG_DEBUG_T("InitEncoder");
    return_error = EbInitEncoder(m_handle);
    if (return_error != EB_ErrorNone) {
        ELOG_ERROR_T("InitEncoder failed, ret 0x%x", return_error);

        EbDeinitHandle(m_handle);
        return -1;
    }

    if (!allocateBuffers()) {
        ELOG_ERROR_T("allocateBuffers failed");

        deallocateBuffers();
        EbDeinitEncoder(m_handle);
        EbDeinitHandle(m_handle);
        return -1;
    }

    if (m_enableBsDump) {
        char dumpFileName[128];

        snprintf(dumpFileName, 128, "/tmp/svtHEVCEncoder-%p.%s", this, "hevc");
        m_bsDumpfp = fopen(dumpFileName, "wb");
        if (m_bsDumpfp) {
            ELOG_DEBUG("Enable bitstream dump, %s", dumpFileName);
        } else {
            ELOG_DEBUG("Can not open dump file, %s", dumpFileName);
        }
    }

    m_frameCount = 0;
    m_dest = dest;
    m_ready = true;
    ELOG_INFO_T("Generate Stream OK!");
    return 0;
}

void SVTHEVCEncoder::degenerateStream(int32_t streamId)
{
    ELOG_INFO_T("%s", __FUNCTION__);

    if (m_ready) {
        EbDeinitEncoder(m_handle);
        EbDeinitHandle(m_handle);

        deallocateBuffers();

        if (m_bsDumpfp) {
            fclose(m_bsDumpfp);
            m_bsDumpfp = NULL;
        }

        m_dest  = NULL;
        m_ready = false;
    }
}

void SVTHEVCEncoder::setBitrate(unsigned short kbps, int32_t streamId)
{
    ELOG_INFO_T("%s", __FUNCTION__);
}

void SVTHEVCEncoder::requestKeyFrame(int32_t streamId)
{
    ELOG_INFO_T("%s", __FUNCTION__);

    m_forceIDR = true;
}

void SVTHEVCEncoder::onFrame(const Frame& frame)
{
    ELOG_TRACE_T("%s", __FUNCTION__);

    EB_ERRORTYPE return_error = EB_ErrorNone;

    if (!m_ready) {
        ELOG_ERROR_T("Encoder not ready!");
        return;
    }

    if (m_freeInputBuffers.empty()) {
        ELOG_WARN_T("No free input buffer available!");
        return;
    }
    EB_BUFFERHEADERTYPE *inputBufferHeader = m_freeInputBuffers.front();
    if (!convert2BufferHeader(frame, inputBufferHeader)) {
        return;
    }

    if (m_forceIDR) {
        inputBufferHeader->sliceType = IDR_SLICE;
        m_forceIDR = false;
    } else {
        inputBufferHeader->sliceType = INVALID_SLICE;
    }

    ELOG_TRACE_T("SendPicture, sliceType(%d)", inputBufferHeader->sliceType);
    return_error = EbH265EncSendPicture(m_handle, inputBufferHeader);
    if (return_error != EB_ErrorNone) {
        ELOG_ERROR_T("SendPicture failed, ret 0x%x", return_error);
        return;
    }

    EB_BUFFERHEADERTYPE *streamBufferHeader = &m_streamBufferPool[0];

    return_error = EbH265GetPacket(m_handle, streamBufferHeader, false);
    if (return_error == EB_ErrorMax) {
        ELOG_ERROR_T("Error while encoding, code 0x%x", streamBufferHeader->nFlags);
        return;
    }else if (return_error != EB_NoErrorEmptyQueue) {
        fillPacketDone(streamBufferHeader);
    }

    //m_freeInputBuffers.pop();
}

bool SVTHEVCEncoder::convert2BufferHeader(const Frame& frame, EB_BUFFERHEADERTYPE *bufferHeader)
{
    EB_H265_ENC_INPUT* inputPtr = (EB_H265_ENC_INPUT*)bufferHeader->pBuffer;

    switch (frame.format) {
        case FRAME_FORMAT_I420: {
            int ret;
            webrtc::VideoFrame *videoFrame = reinterpret_cast<webrtc::VideoFrame*>(frame.payload);
            rtc::scoped_refptr<webrtc::VideoFrameBuffer> videoBuffer = videoFrame->video_frame_buffer();

            ELOG_TRACE_T("Convert frame, %dx%d -> %dx%d"
                    , videoBuffer->width()
                    , videoBuffer->height()
                    , m_encParameters.sourceWidth
                    , m_encParameters.sourceHeight
                    );

            if ((uint32_t)videoBuffer->width() == m_encParameters.sourceWidth
                    && (uint32_t)videoBuffer->height() == m_encParameters.sourceHeight) {
                ret = libyuv::I420Copy(
                        videoBuffer->DataY(), videoBuffer->StrideY(),
                        videoBuffer->DataU(), videoBuffer->StrideU(),
                        videoBuffer->DataV(), videoBuffer->StrideV(),
                        inputPtr->luma, inputPtr->yStride,
                        inputPtr->cb,   inputPtr->cbStride,
                        inputPtr->cr,   inputPtr->crStride,
                        m_encParameters.sourceWidth,
                        m_encParameters.sourceHeight);
                if (ret != 0) {
                    ELOG_ERROR_T("Copy frame failed(%d), %dx%d", ret
                            , m_encParameters.sourceWidth
                            , m_encParameters.sourceHeight
                            );
                    return false;
                }
            } else {
                ret = libyuv::I420Scale(
                        videoBuffer->DataY(),   videoBuffer->StrideY(),
                        videoBuffer->DataU(),   videoBuffer->StrideU(),
                        videoBuffer->DataV(),   videoBuffer->StrideV(),
                        videoBuffer->width(),   videoBuffer->height(),
                        inputPtr->luma, inputPtr->yStride,
                        inputPtr->cb,   inputPtr->cbStride,
                        inputPtr->cr,   inputPtr->crStride,
                        m_encParameters.sourceWidth,
                        m_encParameters.sourceHeight,
                        libyuv::kFilterBox);
                if (ret != 0) {
                    ELOG_ERROR_T("Convert frame failed(%d), %dx%d -> %dx%d", ret
                            , videoBuffer->width()
                            , videoBuffer->height()
                            , m_encParameters.sourceWidth
                            , m_encParameters.sourceHeight
                            );
                    return false;
                }
            }
            break;
        }

        default:
            ELOG_ERROR_T("Unspported video frame format %s(%d)", getFormatStr(frame.format), frame.format);
            return false;
    }

    return true;
}

bool SVTHEVCEncoder::allocateBuffers()
{
    ELOG_INFO_T("%s", __FUNCTION__);

    // one buffer
    m_encParameters.inputOutputBufferFifoInitCount = 1;

    // input buffers
    const size_t luma8bitSize = m_encParameters.sourceWidth * m_encParameters.sourceHeight;
    const size_t chroma8bitSize = luma8bitSize >> 2;

    m_inputBufferPool.resize(m_encParameters.inputOutputBufferFifoInitCount);
    memset(m_inputBufferPool.data(), 0, m_inputBufferPool.size() * sizeof(EB_BUFFERHEADERTYPE));
    for (unsigned int bufferIndex = 0; bufferIndex < m_encParameters.inputOutputBufferFifoInitCount; ++bufferIndex) {
        m_inputBufferPool[bufferIndex].nSize        = sizeof(EB_BUFFERHEADERTYPE);

        m_inputBufferPool[bufferIndex].pBuffer      = (unsigned char *)calloc(1, sizeof(EB_H265_ENC_INPUT));
        if (!m_inputBufferPool[bufferIndex].pBuffer) {
            ELOG_ERROR_T("Can not alloc mem, size(%ld)", sizeof(EB_H265_ENC_INPUT));
            return false;
        }

        // alloc frame
        EB_H265_ENC_INPUT* inputPtr = (EB_H265_ENC_INPUT*)m_inputBufferPool[bufferIndex].pBuffer;
        inputPtr->luma = (unsigned char *)malloc(luma8bitSize);
        if (!inputPtr->luma) {
            ELOG_ERROR_T("Can not alloc mem, size(%ld)", luma8bitSize);
            return false;
        }

        inputPtr->cb = (unsigned char *)malloc(chroma8bitSize);
        if (!inputPtr->cb) {
            ELOG_ERROR_T("Can not alloc mem, size(%ld)", chroma8bitSize);
            return false;
        }

        inputPtr->cr = (unsigned char *)malloc(chroma8bitSize);
        if (!inputPtr->cr) {
            ELOG_ERROR_T("Can not alloc mem, size(%ld)", chroma8bitSize);
            return false;
        }

        inputPtr->yStride   = m_encParameters.sourceWidth;
        inputPtr->crStride  = m_encParameters.sourceWidth >> 1;
        inputPtr->cbStride  = m_encParameters.sourceWidth >> 1;


        m_inputBufferPool[bufferIndex].nAllocLen    = luma8bitSize + chroma8bitSize + chroma8bitSize;
        m_inputBufferPool[bufferIndex].pAppPrivate  = this;
        m_inputBufferPool[bufferIndex].sliceType    = INVALID_SLICE;

        m_freeInputBuffers.push(&m_inputBufferPool[bufferIndex]);
    }

    // output buffers
    size_t outputStreamBufferSize = m_encParameters.sourceWidth * m_encParameters.sourceHeight * 3 / 2;

    m_streamBufferPool.resize(m_encParameters.inputOutputBufferFifoInitCount);
    memset(m_streamBufferPool.data(), 0, m_streamBufferPool.size() * sizeof(EB_BUFFERHEADERTYPE));
    for (uint32_t bufferIndex = 0; bufferIndex < m_encParameters.inputOutputBufferFifoInitCount; ++bufferIndex) {
        m_streamBufferPool[bufferIndex].nSize = sizeof(EB_BUFFERHEADERTYPE);
        m_streamBufferPool[bufferIndex].pBuffer = (unsigned char *)malloc(outputStreamBufferSize);
        if (!m_streamBufferPool[bufferIndex].pBuffer) {
            ELOG_ERROR_T("Can not alloc mem, size(%ld)", outputStreamBufferSize);
            return false;
        }

        m_streamBufferPool[bufferIndex].nAllocLen   = outputStreamBufferSize;
        m_streamBufferPool[bufferIndex].pAppPrivate = this;
        m_streamBufferPool[bufferIndex].sliceType   = INVALID_SLICE;
    }

    return true;
}

void SVTHEVCEncoder::deallocateBuffers()
{
    ELOG_INFO_T("%s", __FUNCTION__);

    for (auto& bufferHeader : m_inputBufferPool) {
        if (bufferHeader.pBuffer) {
            EB_H265_ENC_INPUT* inputPtr = (EB_H265_ENC_INPUT*)bufferHeader.pBuffer;

            if (inputPtr->luma) {
                free(inputPtr->luma);
                inputPtr->luma = NULL;
            }

            if (inputPtr->cb) {
                free(inputPtr->cb);
                inputPtr->cb = NULL;
            }

            if (inputPtr->cr) {
                free(inputPtr->cr);
                inputPtr->cr = NULL;
            }

            free(bufferHeader.pBuffer);
            bufferHeader.pBuffer = NULL;
        }
    }
    m_inputBufferPool.resize(0);

    for (auto& bufferHeader : m_streamBufferPool) {
        if (bufferHeader.pBuffer) {
            free(bufferHeader.pBuffer);
            bufferHeader.pBuffer = NULL;
        }
    }
    m_streamBufferPool.resize(0);
}

void SVTHEVCEncoder::fillPacketDone(EB_BUFFERHEADERTYPE* pBufferHeader)
{
    ELOG_TRACE_T("%s", __FUNCTION__);

    ELOG_DEBUG_T("Fill packet done, nFilledLen(%d), nOffset(%d), nTickCount %d(ms), dts(%lld), pts(%lld), nFlags(0x%x), qpValue(%d), sliceType(%d)"
            , pBufferHeader->nFilledLen
            , pBufferHeader->nOffset
            , pBufferHeader->nTickCount
            , pBufferHeader->dts
            , pBufferHeader->pts
            , pBufferHeader->nFlags
            , pBufferHeader->qpValue
            , pBufferHeader->sliceType
            );

    dump(pBufferHeader->pBuffer + pBufferHeader->nOffset, pBufferHeader->nFilledLen);

    Frame outFrame;
    memset(&outFrame, 0, sizeof(outFrame));
    outFrame.format     = FRAME_FORMAT_H265;
    outFrame.payload    = pBufferHeader->pBuffer + pBufferHeader->nOffset;
    outFrame.length     = pBufferHeader->nFilledLen;
    outFrame.timeStamp = (m_frameCount++) * 1000 / m_encParameters.frameRate * 90;
    outFrame.additionalInfo.video.width         = m_encParameters.sourceWidth;
    outFrame.additionalInfo.video.height        = m_encParameters.sourceHeight;
    outFrame.additionalInfo.video.isKeyFrame    = (pBufferHeader->sliceType == IDR_SLICE);

    ELOG_DEBUG_T("deliverFrame, %s, %dx%d(%s), length(%d)",
            getFormatStr(outFrame.format),
            outFrame.additionalInfo.video.width,
            outFrame.additionalInfo.video.height,
            outFrame.additionalInfo.video.isKeyFrame ? "key" : "delta",
            outFrame.length);

    m_dest->onFrame(outFrame);
}

void SVTHEVCEncoder::dump(uint8_t *buf, int len)
{
    if (m_bsDumpfp) {
        fwrite(buf, 1, len, m_bsDumpfp);
    }
}

} // namespace woogeen_base