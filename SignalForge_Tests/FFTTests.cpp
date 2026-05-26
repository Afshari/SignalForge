#include "pch.h"
#include "WavReader.h"
#include "cpu/SignalForge.h"
#include <filesystem>
#include <vector>
#include <cmath>
#include "TestHelpers.h"


namespace SignalForge {
    // ================================================================================
    // FFTTests - Output shape and basic validity
    // ================================================================================

    TEST(FFTTests, OutputSize_IsHalfFFTSizePlusOne)
    {
        uint32_t fft_size = 65536;
        auto mags = TestHelpers::FFTWavFile(TestHelpers::TestDataPath("engine_clean_500kb_001.wav"), fft_size);
        EXPECT_EQ(mags.size(), fft_size / 2 + 1);
    }

    TEST(FFTTests, AllMagnitudes_AreNonNegative)
    {
        auto mags = TestHelpers::FFTWavFile(TestHelpers::TestDataPath("engine_clean_500kb_001.wav"));
        for (size_t i = 0; i < mags.size(); i++)
            EXPECT_GE(mags[i], 0.0f) << "Negative magnitude at bin " << i;
    }

    TEST(FFTTests, AllMagnitudes_AreFinite)
    {
        auto mags = TestHelpers::FFTWavFile(TestHelpers::TestDataPath("engine_clean_500kb_001.wav"));
        for (size_t i = 0; i < mags.size(); i++)
            EXPECT_TRUE(std::isfinite(mags[i])) << "Non-finite at bin " << i;
    }

    TEST(FFTTests, SomeMagnitudes_AreNonZero)
    {
        auto mags = TestHelpers::FFTWavFile(TestHelpers::TestDataPath("engine_clean_500kb_001.wav"));
        bool any_nonzero = false;
        for (auto m : mags)
            if (m > 0.0f) { any_nonzero = true; break; }
        EXPECT_TRUE(any_nonzero);
    }

    // ================================================================================
    // FFTTests - Clean signal properties
    // ================================================================================

    TEST(FFTTests, CleanSignal_IsDeterministic)
    {
        auto mags_a = TestHelpers::FFTWavFile(TestHelpers::TestDataPath("engine_clean_500kb_001.wav"));
        auto mags_b = TestHelpers::FFTWavFile(TestHelpers::TestDataPath("engine_clean_500kb_001.wav"));
        ASSERT_EQ(mags_a.size(), mags_b.size());

        float max_diff = 0.0f;
        for (size_t i = 0; i < mags_a.size(); i++)
            max_diff = std::max(max_diff, std::abs(mags_a[i] - mags_b[i]));
        EXPECT_LT(max_diff, 1e-5f) << "Max diff: " << max_diff;
    }

    TEST(FFTTests, CleanSignal_TwoFilesMatch)
    {
        auto mags_a = TestHelpers::FFTWavFile(TestHelpers::TestDataPath("engine_clean_500kb_001.wav"));
        auto mags_b = TestHelpers::FFTWavFile(TestHelpers::TestDataPath("engine_clean_500kb_002.wav"));
        ASSERT_EQ(mags_a.size(), mags_b.size());

        float max_diff = 0.0f;
        for (size_t i = 0; i < mags_a.size(); i++)
            max_diff = std::max(max_diff, std::abs(mags_a[i] - mags_b[i]));
        EXPECT_LT(max_diff, 1e-5f) << "Max diff: " << max_diff;
    }

    TEST(FFTTests, CleanSignal_HasPeakAt80Hz)
    {
        uint32_t fft_size = 65536;
        uint32_t sample_rate = 44100;
        uint32_t bin_80hz = (uint32_t)(80.0f * fft_size / sample_rate);

        auto mags = TestHelpers::FFTWavFile(TestHelpers::TestDataPath("engine_clean_500kb_001.wav"), fft_size);

        float peak = 0.0f;
        int   window = 5;
        for (int b = (int)bin_80hz - window; b <= (int)bin_80hz + window; b++)
            if (b >= 0 && b < (int)mags.size())
                peak = std::max(peak, mags[b]);

        EXPECT_GT(peak, 0.0f) << "No peak near 80Hz bin " << bin_80hz;
    }

    // ================================================================================
    // FFTTests - Noisy vs clean
    // ================================================================================

    TEST(FFTTests, NoisySignal_DiffersFromClean)
    {
        auto mags_clean = TestHelpers::FFTWavFile(TestHelpers::TestDataPath("engine_clean_500kb_001.wav"));
        auto mags_noisy = TestHelpers::FFTWavFile(TestHelpers::TestDataPath("engine_noisy_500kb_001.wav"));
        ASSERT_EQ(mags_clean.size(), mags_noisy.size());

        bool any_different = false;
        for (size_t i = 0; i < mags_clean.size(); i++)
            if (std::abs(mags_clean[i] - mags_noisy[i]) > 1e-6f)
            {
                any_different = true; break;
            }

        EXPECT_TRUE(any_different);
    }

    TEST(FFTTests, NoisySignal_TwoFilesAreDifferent)
    {
        auto mags_a = TestHelpers::FFTWavFile(TestHelpers::TestDataPath("engine_noisy_500kb_001.wav"));
        auto mags_b = TestHelpers::FFTWavFile(TestHelpers::TestDataPath("engine_noisy_500kb_002.wav"));
        ASSERT_EQ(mags_a.size(), mags_b.size());

        bool any_different = false;
        for (size_t i = 0; i < mags_a.size(); i++)
            if (std::abs(mags_a[i] - mags_b[i]) > 1e-6f)
            {
                any_different = true; break;
            }

        EXPECT_TRUE(any_different);
    }

    // ================================================================================
    // FFTTests - Anomaly signal
    // ================================================================================

    TEST(FFTTests, AnomalySignal_HasStrongerPeakAt1350Hz_ThanClean)
    {
        uint32_t fft_size = 65536;
        uint32_t sample_rate = 44100;
        uint32_t bin_1350hz = (uint32_t)(1350.0f * fft_size / sample_rate);

        auto mags_anomaly = TestHelpers::FFTWavFile(TestHelpers::TestDataPath("engine_anomaly_500kb_001.wav"), fft_size);
        auto mags_clean = TestHelpers::FFTWavFile(TestHelpers::TestDataPath("engine_clean_500kb_001.wav"), fft_size);

        float peak_anomaly = 0.0f;
        float peak_clean = 0.0f;
        int   window = 5;
        for (int b = (int)bin_1350hz - window; b <= (int)bin_1350hz + window; b++)
        {
            if (b >= 0 && b < (int)mags_anomaly.size())
            {
                peak_anomaly = std::max(peak_anomaly, mags_anomaly[b]);
                peak_clean = std::max(peak_clean, mags_clean[b]);
            }
        }
        EXPECT_GT(peak_anomaly, peak_clean)
            << "Anomaly 1350Hz peak=" << peak_anomaly
            << " clean 1350Hz peak=" << peak_clean;
    }

    // ================================================================================
    // FFTTests - Batch consistency
    // ================================================================================

    TEST(FFTTests, BatchFFT_MatchesSingleFFT)
    {
        SignalForge::WavReader reader_a(TestHelpers::TestDataPath("engine_clean_500kb_001.wav"));
        SignalForge::WavReader reader_b(TestHelpers::TestDataPath("engine_clean_500kb_002.wav"));

        auto pcm_a = reader_a.ReadPCM();
        auto pcm_b = reader_b.ReadPCM();

        uint32_t fft_size = 65536;
        uint32_t threads_per_block = 128;
        uint32_t half = fft_size / 2 + 1;

        // Single FFT
        std::vector<std::vector<uint8_t>> input_a = { pcm_a };
        std::vector<std::vector<uint8_t>> input_b = { pcm_b };
        std::vector<float> single_a(half, 0.0f);
        std::vector<float> single_b(half, 0.0f);
        FFTBatchWrapper_CPU(input_a, single_a.data(), 1, fft_size, threads_per_block);
        FFTBatchWrapper_CPU(input_b, single_b.data(), 1, fft_size, threads_per_block);

        // Batch FFT
        std::vector<std::vector<uint8_t>> inputs = { pcm_a, pcm_b };
        std::vector<float> batch(half * 2, 0.0f);
        FFTBatchWrapper_CPU(inputs, batch.data(), 2, fft_size, threads_per_block);

        float max_diff_a = 0.0f;
        for (uint32_t i = 0; i < half; i++)
            max_diff_a = std::max(max_diff_a, std::abs(batch[i] - single_a[i]));
        EXPECT_LT(max_diff_a, 1e-5f) << "File A mismatch, max diff=" << max_diff_a;

        float max_diff_b = 0.0f;
        for (uint32_t i = 0; i < half; i++)
            max_diff_b = std::max(max_diff_b, std::abs(batch[half + i] - single_b[i]));
        EXPECT_LT(max_diff_b, 1e-5f) << "File B mismatch, max diff=" << max_diff_b;
    }

} // namespace SignalForge