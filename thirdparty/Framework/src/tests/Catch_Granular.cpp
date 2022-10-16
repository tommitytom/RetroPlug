#include <catch/catch.hpp>

#include "audio/Granular.h"

#include <iostream>

using namespace fw;

TEST_CASE("Grain Stream", "[Granular]") {
	const uint32 frameCount = 50;
	const uint32 grainSize = 5;

	StereoAudioBuffer input(frameCount, 48000);

	for (uint32 i = 0; i < frameCount; ++i) {
		input.setSample(i, 0, (AudioSampleT)i);
		input.setSample(i, 1, (AudioSampleT)i);
	}

	for (uint32 i = 0; i < frameCount; ++i) {
		std::cout << i << " :: " << input.getSample(i, 0) << ", " << input.getSample(i, 1) << std::endl;
	}
	
	GrainStream stream;
	stream.add(Grain{ .buffer = input.ref(), .window = generateWindow(Envelopes::none, grainSize) });
	stream.add(Grain{ .buffer = input.ref(), .window = generateWindow(Envelopes::none, grainSize), .delay = grainSize });

	StereoAudioBuffer out(grainSize, 48000);
	stream.process(out);

	for (uint32 i = 0; i < out.getFrameCount(); ++i) {
		std::cout << i << " :: " << out.getSample(i, 0) << ", " << out.getSample(i, 1) << std::endl;
	}
	std::cout << std::endl;

	for (uint32 i = 0; i < out.getFrameCount(); ++i) {
		REQUIRE((uint32)out.getSample(i, 0) == i);
		REQUIRE((uint32)out.getSample(i, 1) == i);
	}

	stream.process(out);

	for (uint32 i = 0; i < out.getFrameCount(); ++i) {
		REQUIRE((uint32)out.getSample(i, 0) == i);
		REQUIRE((uint32)out.getSample(i, 1) == i);
	}

	stream.add(Grain{ .buffer = input.ref(), .window = generateWindow(Envelopes::none, grainSize * 2), .speed = 0.5f });

	StereoAudioBuffer out2(grainSize * 2, 48000);
	stream.process(out2);

	for (uint32 i = 0; i < out2.getFrameCount(); ++i) {
		std::cout << i << " :: " << out2.getSample(i, 0) << ", " << out2.getSample(i, 1) << std::endl;
	}
	std::cout << std::endl;

	for (uint32 i = 0; i < out2.getFrameCount(); ++i) {
		REQUIRE(out2.getSample(i, 0) == i * 0.5f);
		REQUIRE(out2.getSample(i, 1) == i * 0.5f);
	}

	std::cout << "-----------" << std::endl;
}

TEST_CASE("Granular Time Stretch", "[Granular]") {
	const uint32 frameCount = 50;
	const uint32 grainSize = 10;

	StereoAudioBuffer inputOne(frameCount, 48000);
	StereoAudioBuffer inputSeq(frameCount, 48000);

	for (uint32 i = 0; i < frameCount; ++i) {
		inputSeq.setSample(i, 0, (AudioSampleT)i);
		inputSeq.setSample(i, 1, (AudioSampleT)i);
		inputOne.setSample(i, 0, 1.0f);
		inputOne.setSample(i, 1, 1.0f);
	}

	GranularTimeStretch timestretch(inputOne.ref());
	timestretch.setGrainSize(grainSize);
	timestretch.setOverlap(0.25f);

	StereoAudioBuffer out(grainSize * 2, 48000);
	timestretch.process(out);

	for (uint32 i = 0; i < out.getFrameCount(); ++i) {
		std::cout << i << " :: " << out.getSample(i, 0) << ", " << out.getSample(i, 1) << std::endl;
	}
	std::cout << std::endl;
}
