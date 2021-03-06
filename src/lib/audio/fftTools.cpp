/***************************************************************************
 *   Copyright (C) 2010 by Simon Andreas Eugster (simon.eu@gmail.com)      *
 *   This file is part of kdenlive. See www.kdenlive.org.                  *
 *                                                                         *
 *   This program is free software; you can redistribute it and/or modify  *
 *   it under the terms of the GNU General Public License as published by  *
 *   the Free Software Foundation; either version 2 of the License, or     *
 *   (at your option) any later version.                                   *
 ***************************************************************************/

#include "fftTools.h"

#include <math.h>
#include <iostream>

#include <QString>

// Uncomment for debugging, like writing a GNU Octave .m file to /tmp
//#define DEBUG_FFTTOOLS

#ifdef DEBUG_FFTTOOLS
#include <QDebug>
#include <QTime>
#include <fstream>
#endif

FFTTools::FFTTools() :
        m_fftCfgs(),
        m_windowFunctions()
{
}
FFTTools::~FFTTools()
{
    QHash<QString, kiss_fftr_cfg>::iterator i;
    for (i = m_fftCfgs.begin(); i != m_fftCfgs.end(); ++i) {
        free(*i);
    }
}

const QString FFTTools::windowSignature(const WindowType windowType, const int size, const float param)
{
    return QStringLiteral("s%1_t%2_p%3").arg(size).arg(windowType).arg(param, 0, 'f', 3);
}
const QString FFTTools::cfgSignature(const int size)
{
    return QStringLiteral("s%1").arg(size);
}

// http://cplusplus.syntaxerrors.info/index.php?title=Cannot_declare_member_function_%E2%80%98static_int_Foo::bar%28%29%E2%80%99_to_have_static_linkage
const QVector<float> FFTTools::window(const WindowType windowType, const int size, const float param)
{
    Q_ASSERT(size > 0);

    // Deliberately avoid converting size to a float
    // to keep mid an integer.
    float mid = (int) (size-1)/2;
    float max = size-1;
    QVector<float> window;

    switch (windowType) {
    case Window_Rect:
        return QVector<float>(size+1, 1);
        break;
    case Window_Triangle:
        window = QVector<float>(size+1);

        for (int x = 0; x < mid; ++x) {
            window[x] = x/mid + (mid-x)/mid*param;
        }
        for (int x = mid; x < size; ++x) {
            window[x] = (x-mid)/(max-mid) * param + (max-x)/(max-mid);
        }
        window[size] = .5 + param/2;

#ifdef DEBUG_FFTTOOLS
        qDebug() << "Triangle window (factor " << window[size] << "):";
        for (int i = 0; i < size; ++i) {
            qDebug() << window[i];
        }
        qDebug() << "Triangle window end.";
#endif

        return window;
        break;
    case Window_Hamming:
        // Use a quick version of the Hamming window here: Instead of
        // interpolating values between (-max/2) and (max/2)
        // we use integer values instead, ranging from -mid to (max-mid).
        window = QVector<float>(size+1);

        for (int x = 0; x < size; ++x) {
            window[x] = .54 + .46 * cos( 2*M_PI*(x-mid) / size );
        }

        // Integrating the cosine over the window function results in
        // an area of 0; So only the constant factor 0.54 counts.
        window[size] = .54;

#ifdef DEBUG_FFTTOOLS
        qDebug() << "Hanning window (factor " << window[size] << "):";
        for (int i = 0; i < size; ++i) {
            qDebug() << window[i];
        }
        qDebug() << "Hanning window end.";
#endif

        return window;
        break;
    }
    Q_ASSERT(false);
    return QVector<float>();
}

void FFTTools::fftNormalized(const audioShortVector audioFrame, const uint channel, const uint numChannels, float *freqSpectrum,
                             const WindowType windowType, const uint windowSize, const float param)
{
#ifdef DEBUG_FFTTOOLS
    QTime start = QTime::currentTime();
#endif

    const uint numSamples = audioFrame.size()/numChannels;

	if (windowSize & 1 || windowSize < 2)
	    return;

    const QString cfgSig = cfgSignature(windowSize);
    const QString winSig = windowSignature(windowType, windowSize, param);


    // Get the kiss_fft configuration from the config cache
    // or build a new configuration if the requested one is not available.
    kiss_fftr_cfg myCfg;
    if (m_fftCfgs.contains(cfgSig)) {
#ifdef DEBUG_FFTTOOLS
        qDebug() << "Re-using FFT configuration with size " << windowSize;
#endif
        myCfg = m_fftCfgs.value(cfgSig);
    } else {
#ifdef DEBUG_FFTTOOLS
        qDebug() << "Creating FFT configuration with size " << windowSize;
#endif
        myCfg = kiss_fftr_alloc(windowSize, false,NULL,NULL);
        m_fftCfgs.insert(cfgSig, myCfg);
    }

    // Get the window function from the cache
    // (except for a rectangular window; nothing to to there.
    QVector<float> window;
    float windowScaleFactor = 1;
    if (windowType != FFTTools::Window_Rect) {

        if (m_windowFunctions.contains(winSig)) {
#ifdef DEBUG_FFTTOOLS
            qDebug() << "Re-using window function with signature " << winSig;
#endif
            window = m_windowFunctions.value(winSig);
        } else {
#ifdef DEBUG_FFTTOOLS
            qDebug() << "Building new window function with signature " << winSig;
#endif
            window = FFTTools::window(windowType, windowSize, 0);
            m_windowFunctions.insert(winSig, window);
        }
        windowScaleFactor = 1.0/window[windowSize];
    }


    // Prepare frequency space vector. The resulting FFT vector is only half as long.
    kiss_fft_cpx freqData[windowSize/2];
    float data[windowSize];

    // Copy the first channel's audio into a vector for the FFT display;
    // Fill the data vector indices that cannot be covered with sample data with 0
    if (numSamples < windowSize) {
        std::fill(&data[numSamples], &data[windowSize-1], 0);
    }
    // Normalize signals to [0,1] to get correct dB values later on
    for (uint i = 0; i < numSamples && i < windowSize; ++i) {
        // Performance note: Benchmarking has shown that using the if/else inside the loop
        // does not do noticeable worse than keeping it outside (perhaps the branch predictor
        // is good enough), so it remains in there for better readability.
        if (windowType != FFTTools::Window_Rect) {
            data[i] = (float) audioFrame.data()[i*numChannels + channel] / 32767.0f * window[i];
        } else {
            data[i] = (float) audioFrame.data()[i*numChannels + channel] / 32767.0f;
        }
    }

    // Calculate the Fast Fourier Transform for the input data
    kiss_fftr(myCfg, data, freqData);


    // Logarithmic scale: 20 * log ( 2 * magnitude / N ) with magnitude = sqrt(r² + i²)
    // with N = FFT size (after FFT, 1/2 window size)
    for (uint i = 0; i < windowSize/2; ++i) {
        // Logarithmic scale: 20 * log ( 2 * magnitude / N ) with magnitude = sqrt(r² + i²)
        // with N = FFT size (after FFT, 1/2 window size)
        freqSpectrum[i] = 20*log(pow(pow(fabs(freqData[i].r * windowScaleFactor),2) + pow(fabs(freqData[i].i * windowScaleFactor),2), .5)/((float)windowSize/2.0f))/log(10);;
    }


#ifdef DEBUG_FFTTOOLS
    std::ofstream mFile;
    mFile.open("/tmp/freq.m");
    if (!mFile) {
        qDebug() << "Opening file failed.";
    } else {
        mFile << "val = [ ";

        for (int sample = 0; sample < 256; ++sample) {
            mFile << data[sample] << ' ';
        }
        mFile << " ];\n";

        mFile << "freq = [ ";
        for (int sample = 0; sample < 256; ++sample) {
            mFile << freqData[sample].r << '+' << freqData[sample].i << "*i ";
        }
        mFile << " ];\n";

        mFile.close();
        qDebug() << "File written.";
    }
#endif

#ifdef DEBUG_FFTTOOLS
    qDebug() << "Calculated FFT in " << start.elapsed() << " ms.";
#endif
}

const QVector<float> FFTTools::interpolatePeakPreserving(const QVector<float> in, const uint targetSize, uint left, uint right, float fill)
{
#ifdef DEBUG_FFTTOOLS
    QTime start = QTime::currentTime();
#endif

    if (right == 0) {
        right = in.size()-1;
    }
    Q_ASSERT(targetSize > 0);
    Q_ASSERT(left < right);

    QVector<float> out(targetSize);


    float x;
    uint xi;
    uint i;
    if (((float) (right-left))/targetSize < 2) {
        float x_prev = 0;
        for (i = 0; i < targetSize; ++i) {

            // i:  Target index
            // x:  Interpolated source index (float!)
            // xi: floor(x)

            // Transform [0,targetSize-1] to [left,right]
            x = ((float) i) / (targetSize-1) * (right-left) + left;
            xi = (int) floor(x);

            if (x > in.size()-1) {
                // This may happen if right > in.size()-1; Fill the rest of the vector
                // with the default value now.
                break;
            }


            // Use linear interpolation in order to get smoother display
            if (xi == 0 || xi == (uint) in.size()-1) {
                // ... except if we are at the left or right border of the input sigal.
                // Special case here since we consider previous and future values as well for
                // the actual interpolation (not possible here).
                out[i] = in[xi];
            } else {
                if (in[xi] > in[xi+1]
                    && x_prev < xi) {
                    // This is a hack to preserve peaks.
                    // Consider f = {0, 100, 0}
                    //          x = {0.5,  1.5}
                    // Then x is 50 both times, and the 100 peak is lost.
                    // Get it back here for the first x after the peak (which is at xi).
                    // (x is the first after the peak if the previous x was smaller than floor(x).)
                    out[i] = in[xi];
                } else {
                    out[i] =   (xi+1 - x) * in[xi]
                          + (x - xi)   * in[xi+1];
                }
            }
            x_prev = x;
        }
    } else {
        // If there are more than 2 samples per pixel in average, then use the maximum of them
        // since by only looking at the left sample we might miss some maxima.
        uint src = left;
        for (i = 0; i < targetSize; ++i) {

            // x:  right bound
            // xi: floor(x)
            x = ((float) (i+1)) / (targetSize-1) * (right-left) + left;
            xi = (int) floor(x);
            int points = 0;

            out[i] = fill;

            for (; src < xi && src < (uint) in.size(); ++src) {
                if (out[i] < in[src]) {
                    out[i] = in[src];
                }
                points++;
            }

//             xi_prev = xi;
        }
    }
    // Fill the rest of the vector if the right border exceeds the input vector.
    for (; i < targetSize; ++i) {
        out[i] = fill;
    }

#ifdef DEBUG_FFTTOOLS
    qDebug() << "Interpolated " << targetSize << " nodes from " << in.size() << " input points in " << start.elapsed() << " ms";
#endif

    return out;
}

#ifdef DEBUG_FFTTOOLS
#undef DEBUG_FFTTOOLS
#endif
