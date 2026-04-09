#include "../third_party/doctest/doctest.h"

#include "PartFilePauseResumeSeams.h"

namespace
{
	using PartFilePauseResumeSeams::RuntimeStatus;
	using PartFilePauseResumeSeams::State;
	using PartFilePauseResumeSeams::TransitionResult;
	using PartFilePauseResumeSeams::VisibleStatus;

	State MakeState(const RuntimeStatus eStatus, const bool bPaused, const bool bInsufficient, const bool bStopped)
	{
		State state = { eStatus, bPaused, bInsufficient, bStopped };
		return state;
	}

	void CheckTransitionState(const TransitionResult &rResult, const bool bPaused, const bool bInsufficient, const bool bStopped)
	{
		CHECK_EQ(rResult.NextState.Paused, bPaused);
		CHECK_EQ(rResult.NextState.Insufficient, bInsufficient);
		CHECK_EQ(rResult.NextState.Stopped, bStopped);
	}
}

TEST_SUITE_BEGIN("parity");

TEST_CASE("Part-file pause-resume seam resolves visible status from the active-state flags")
{
	CHECK_EQ(PartFilePauseResumeSeams::ResolveVisibleStatus(RuntimeStatus::Active, false, false), VisibleStatus::Active);
	CHECK_EQ(PartFilePauseResumeSeams::ResolveVisibleStatus(RuntimeStatus::Active, true, false), VisibleStatus::Paused);
	CHECK_EQ(PartFilePauseResumeSeams::ResolveVisibleStatus(RuntimeStatus::Active, false, true), VisibleStatus::Insufficient);
	CHECK_EQ(PartFilePauseResumeSeams::ResolveVisibleStatus(RuntimeStatus::Active, true, true), VisibleStatus::Paused);
	CHECK_EQ(PartFilePauseResumeSeams::ResolveVisibleStatus(RuntimeStatus::Active, true, true, true), VisibleStatus::Active);
}

TEST_CASE("Part-file pause-resume seam preserves error and completion visibility regardless of pause flags")
{
	CHECK_EQ(PartFilePauseResumeSeams::ResolveVisibleStatus(RuntimeStatus::Error, true, true), VisibleStatus::Error);
	CHECK_EQ(PartFilePauseResumeSeams::ResolveVisibleStatus(RuntimeStatus::Completing, true, true), VisibleStatus::Completing);
	CHECK_EQ(PartFilePauseResumeSeams::ResolveVisibleStatus(RuntimeStatus::Complete, true, true), VisibleStatus::Complete);
}

TEST_CASE("Part-file pause-resume seam models normal and insufficient pause transitions")
{
	const TransitionResult paused = PartFilePauseResumeSeams::ApplyPauseTransition(MakeState(RuntimeStatus::Active, false, false, false), false);
	CHECK_FALSE(paused.IsNoOp);
	CheckTransitionState(paused, true, false, false);
	CHECK(paused.ShouldNotifyStatusChange);
	CHECK(paused.ShouldSavePartFile);

	const TransitionResult insufficient = PartFilePauseResumeSeams::ApplyPauseTransition(MakeState(RuntimeStatus::Active, false, false, false), true);
	CHECK_FALSE(insufficient.IsNoOp);
	CheckTransitionState(insufficient, false, true, false);
	CHECK(insufficient.ShouldNotifyStatusChange);
	CHECK_FALSE(insufficient.ShouldSavePartFile);

	const TransitionResult pausedThenInsufficient = PartFilePauseResumeSeams::ApplyPauseTransition(MakeState(RuntimeStatus::Active, true, false, false), true);
	CHECK_FALSE(pausedThenInsufficient.IsNoOp);
	CheckTransitionState(pausedThenInsufficient, true, true, false);
	CHECK(pausedThenInsufficient.ShouldNotifyStatusChange);
	CHECK_FALSE(pausedThenInsufficient.ShouldSavePartFile);
}

TEST_CASE("Part-file pause-resume seam keeps repeated insufficient pause and complete-like pause as no-ops")
{
	const TransitionResult repeatedInsufficient = PartFilePauseResumeSeams::ApplyPauseTransition(MakeState(RuntimeStatus::Active, false, true, false), true);
	CHECK(repeatedInsufficient.IsNoOp);
	CheckTransitionState(repeatedInsufficient, false, true, false);
	CHECK_FALSE(repeatedInsufficient.ShouldNotifyStatusChange);
	CHECK_FALSE(repeatedInsufficient.ShouldSavePartFile);

	const TransitionResult completingPause = PartFilePauseResumeSeams::ApplyPauseTransition(MakeState(RuntimeStatus::Completing, false, false, false), false);
	CHECK(completingPause.IsNoOp);
	CheckTransitionState(completingPause, false, false, false);

	const TransitionResult completeInsufficientPause = PartFilePauseResumeSeams::ApplyPauseTransition(MakeState(RuntimeStatus::Complete, false, false, false), true);
	CHECK(completeInsufficientPause.IsNoOp);
	CheckTransitionState(completeInsufficientPause, false, false, false);
}

TEST_CASE("Part-file pause-resume seam models normal and insufficient resume asymmetrically")
{
	const TransitionResult normalResume = PartFilePauseResumeSeams::ApplyNormalResumeTransition(MakeState(RuntimeStatus::Active, true, false, true));
	CHECK_FALSE(normalResume.IsNoOp);
	CheckTransitionState(normalResume, false, false, false);
	CHECK(normalResume.ShouldNotifyStatusChange);
	CHECK(normalResume.ShouldSavePartFile);

	const TransitionResult insufficientResume = PartFilePauseResumeSeams::ApplyInsufficientResumeTransition(MakeState(RuntimeStatus::Active, false, true, false));
	CHECK_FALSE(insufficientResume.IsNoOp);
	CheckTransitionState(insufficientResume, false, false, false);
	CHECK_FALSE(insufficientResume.ShouldNotifyStatusChange);
	CHECK_FALSE(insufficientResume.ShouldSavePartFile);

	const TransitionResult pausedInsufficientResume = PartFilePauseResumeSeams::ApplyInsufficientResumeTransition(MakeState(RuntimeStatus::Active, true, true, false));
	CHECK_FALSE(pausedInsufficientResume.IsNoOp);
	CheckTransitionState(pausedInsufficientResume, true, false, false);
	CHECK_FALSE(pausedInsufficientResume.ShouldNotifyStatusChange);
	CHECK_FALSE(pausedInsufficientResume.ShouldSavePartFile);

	const TransitionResult directResumeFromInsufficient = PartFilePauseResumeSeams::ApplyNormalResumeTransition(MakeState(RuntimeStatus::Active, false, true, false));
	CHECK_FALSE(directResumeFromInsufficient.IsNoOp);
	CheckTransitionState(directResumeFromInsufficient, false, true, false);
	CHECK(directResumeFromInsufficient.ShouldNotifyStatusChange);
	CHECK(directResumeFromInsufficient.ShouldSavePartFile);
}

TEST_CASE("Part-file pause-resume seam keeps completion-error resumes distinct from ordinary resumes")
{
	CHECK(PartFilePauseResumeSeams::UsesCompletionErrorResumePath(RuntimeStatus::Error, true));
	CHECK_FALSE(PartFilePauseResumeSeams::UsesCompletionErrorResumePath(RuntimeStatus::Error, false));
	CHECK_FALSE(PartFilePauseResumeSeams::UsesCompletionErrorResumePath(RuntimeStatus::Active, true));
}

TEST_CASE("Part-file pause-resume seam models stop as paused stopped and not insufficient")
{
	const TransitionResult stoppedFromInsufficient = PartFilePauseResumeSeams::ApplyStopTransition(MakeState(RuntimeStatus::Active, false, true, false));
	CHECK_FALSE(stoppedFromInsufficient.IsNoOp);
	CheckTransitionState(stoppedFromInsufficient, true, false, true);
	CHECK(stoppedFromInsufficient.ShouldNotifyStatusChange);
	CHECK(stoppedFromInsufficient.ShouldSavePartFile);

	const TransitionResult stoppedFromComplete = PartFilePauseResumeSeams::ApplyStopTransition(MakeState(RuntimeStatus::Complete, false, true, false));
	CHECK_FALSE(stoppedFromComplete.IsNoOp);
	CheckTransitionState(stoppedFromComplete, true, false, true);
	CHECK_FALSE(stoppedFromComplete.ShouldNotifyStatusChange);
	CHECK_FALSE(stoppedFromComplete.ShouldSavePartFile);
}

TEST_SUITE_END;
