#pragma once
#include <cmath>
#include <vector>
#include <array>
#include "PRM.h"
#include <juce_audio_basics/juce_audio_basics.h>

namespace envfol
{
	using Smooth = smooth::Smooth<double>;
	using PRM = dsp::PRMD;
	using PRMInfo = dsp::PRMInfoD;
	using SIMD = juce::FloatVectorOperations;

	inline double msInSamples(double ms, double Fs) noexcept
	{
		return ms * .001 * Fs;
	}

	inline double dbToAmp(double db) noexcept
	{
		return std::pow(10., db * .05);
	}

	struct EnvFol
	{
		EnvFol() :
			atkPRM(1.),
			rlsPRM(1.),
			gainPRM(0.),
			widthPRM(0.),
			envelope{ 0., 0. },
			envSmooth{ 0., 0. },
			sampleRate(1.)
		{}

		void prepare(double _sampleRate, int blockSize)
		{
			sampleRate = _sampleRate;
			atkPRM.prepare(sampleRate, blockSize, 10.);
			rlsPRM.prepare(sampleRate, blockSize, 10.);
			gainPRM.prepare(sampleRate, blockSize, 10.);
			widthPRM.prepare(sampleRate, blockSize, 10.);
			for (auto ch = 0; ch < 2; ++ch)
				envSmooth[ch].makeFromDecayInMs(20., sampleRate);
		}

		void operator()(double* const* samples,
			const double* const* samplesSC,
			double attackMs, double releaseMs, double gainDb, double width,
			int numChannels, int numSamples, bool scEnabled) noexcept
		{
			const auto atkBuf = synthesizeAtkBuf(attackMs, numSamples);
			const auto rlsBuf = synthesizeRlsBuf(releaseMs, numSamples);
			const auto gainBuf = synthesizeGainBuf(gainDb, atkBuf, rlsBuf, numSamples);
			
			const auto samplesInput = scEnabled ? samplesSC : samples;

			synthesizeEnvelope(samples[0], samplesInput[0], atkBuf, rlsBuf, gainBuf, envelope[0], numSamples);
			
			if (numChannels == 2)
			{
				synthesizeEnvelope(samples[1], samplesInput[1], atkBuf, rlsBuf, gainBuf, envelope[1], numSamples);
				processWidth(samples, width, numSamples);
			}
			
			smoothen(samples, numChannels, numSamples);
			makeBipolar(samples, numChannels, numSamples);
		}

	protected:
		PRM atkPRM, rlsPRM, gainPRM, widthPRM;
		std::array<double, 2> envelope;
		std::array<Smooth, 2> envSmooth;
		double sampleRate;

		const double* synthesizeAtkBuf(double attackMs, int numSamples) noexcept
		{
			const auto atkSamples = msInSamples(attackMs, sampleRate);
			const auto atk = 1. / atkSamples;
			auto atkInfo = atkPRM(atk, numSamples);
			if (!atkInfo.smoothing)
				SIMD::fill(atkInfo.buf, atk, numSamples);
			return atkInfo.buf;
		}

		const double* synthesizeRlsBuf(double releaseMs, int numSamples) noexcept
		{
			const auto rlsSamples = msInSamples(releaseMs, sampleRate);
			const auto rls = 1. / rlsSamples;
			auto rlsInfo = atkPRM(rls, numSamples);
			if (!rlsInfo.smoothing)
				SIMD::fill(rlsInfo.buf, rls, numSamples);
			return rlsInfo.buf;
		}

		const double* synthesizeGainBuf(double gainDb,
			const double* atkBuf, const double* rlsBuf, int numSamples) noexcept
		{
			const auto gainAmp = dbToAmp(gainDb);
			auto gainInfo = gainPRM(gainAmp, numSamples);
			auto gainBuf = gainInfo.buf;
			if (!gainInfo.smoothing)
				SIMD::fill(gainBuf, gainAmp, numSamples);

			for (auto s = 0; s < numSamples; ++s)
			{
				const auto atk = atkBuf[s];
				const auto rls = rlsBuf[s];
				const auto autogain = atk != 0. ? 1. + std::sqrt(rls / atk) : 1.;
				gainBuf[s] *= autogain;
			}

			return gainBuf;
		}
		
		void synthesizeEnvelope(double* smpls, const double* smplsSC,
			const double* atkBuf, const double* rlsBuf, const double* gainBuf,
			double& env, int numSamples) noexcept
		{
			for (auto s = 0; s < numSamples; ++s)
			{
				const auto smpl = gainBuf[s] * smplsSC[s] * smplsSC[s];
				if (env < smpl)
					env += atkBuf[s] * (smpl - env);
				else
					env += rlsBuf[s] * (smpl - env);
				smpls[s] = env * gainBuf[s];
			}
		}

		void processWidth(double* const* samples, double width,
			int numSamples) noexcept
		{
			const auto widthInfo = widthPRM(width, numSamples);
			const auto smplsL = samples[0];
			auto smplsR = samples[1];

			if (widthInfo.smoothing)
				for (auto s = 0; s < numSamples; ++s)
					smplsR[s] = smplsL[s] + widthInfo.buf[s] * (smplsR[s] - smplsL[s]);
			else
			{
				if (widthInfo.val == 0.)
					SIMD::copy(smplsR, smplsL, numSamples);
				else
					for (auto s = 0; s < numSamples; ++s)
						smplsR[s] = smplsL[s] + widthInfo.val * (smplsR[s] - smplsL[s]);
			}
		}

		void smoothen(double* const* samples, int numChannels, int numSamples) noexcept
		{
			for (auto ch = 0; ch < numChannels; ++ch)
			{
				auto smpls = samples[ch];
				auto& smooth = envSmooth[ch];

				for (auto s = 0; s < numSamples; ++s)
					smpls[s] = smooth(smpls[s] < 1. ? smpls[s] : 1.);
			}
		}

		void makeBipolar(double* const* samples, int numChannels, int numSamples) noexcept
		{
			for (auto ch = 0; ch < numChannels; ++ch)
			{
				auto smpls = samples[ch];
				SIMD::multiply(smpls, 2., numSamples);
				for (auto s = 0; s < numSamples; ++s)
					smpls[s] -= 1.;
			}
		}
	};
}