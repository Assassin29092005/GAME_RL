// Hellwalker — engine-free core. The done-tests of PLAN §3, as plain functions.
//
// Each returns true on pass and appends a human-readable log. The simulator runs them from the
// terminal (Sim/ThesisSim --tests); the Unreal module wraps each one in an automation test
// ("Project.HellwalkerRL.Core.<Name>") so the same assertions run headless inside the editor.

#pragma once

#include <cstdint>
#include <string>

namespace HW
{
	using FCoreTestFn = bool (*)(std::string& Log);

	struct FCoreTest
	{
		const char*  Name;
		const char*  Milestone;
		FCoreTestFn  Fn;
	};

	/** All core tests, in plan order. */
	int32_t GetCoreTests(const FCoreTest*& OutTests);
}
