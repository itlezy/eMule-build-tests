#include "../third_party/doctest/doctest.h"

#include "UploadScoreSeams.h"

TEST_SUITE_BEGIN("parity");

TEST_CASE("Upload score seam restores stale low-ratio ordering before divisor and old-client penalty")
{
	UploadScoreSeams::UploadScoreInputs inputs = {};
	inputs.uBaseValueMs = 100000u;
	inputs.fCreditRatio = 1.0f;
	inputs.iFilePrioNumber = 7;
	inputs.bUseCreditSystem = false;
	inputs.bApplyPriority = false;
	inputs.bApplyLowRatioBonus = true;
	inputs.uLowRatioBonus = 50u;
	inputs.bApplyLowIdDivisor = true;
	inputs.uLowIdDivisor = 2u;
	inputs.bApplyOldClientPenalty = true;

	const UploadScoreSeams::UploadScoreBreakdown breakdown = UploadScoreSeams::BuildUploadScoreBreakdown(inputs);
	CHECK_EQ(breakdown.eAvailability, UploadScoreSeams::uploadScoreAvailable);
	CHECK_EQ(breakdown.uBaseScore, 50u);
	CHECK_EQ(breakdown.uEffectiveScore, 37u);
	CHECK(breakdown.bLowRatioApplied);
	CHECK(breakdown.bLowIdPenaltyApplied);
	CHECK(breakdown.bOldClientPenaltyApplied);
}

TEST_CASE("Upload score seam applies the low-ratio bonus before the legacy old-client penalty")
{
	UploadScoreSeams::UploadScoreInputs inputs = {};
	inputs.uBaseValueMs = 100000u;
	inputs.fCreditRatio = 1.0f;
	inputs.iFilePrioNumber = 7;
	inputs.bUseCreditSystem = false;
	inputs.bApplyPriority = false;
	inputs.bApplyLowRatioBonus = true;
	inputs.uLowRatioBonus = 50u;
	inputs.bApplyLowIdDivisor = false;
	inputs.uLowIdDivisor = 1u;
	inputs.bApplyOldClientPenalty = true;

	const UploadScoreSeams::UploadScoreBreakdown breakdown = UploadScoreSeams::BuildUploadScoreBreakdown(inputs);
	CHECK_EQ(breakdown.uBaseScore, 50u);
	CHECK_EQ(breakdown.uEffectiveScore, 75u);
}

TEST_CASE("Upload score seam preserves the stock combined credit and file-priority multiplier")
{
	CHECK(UploadScoreSeams::ComputeCombinedFilePrioAndCredit(1.0f, 7) == doctest::Approx(70.0f));
	CHECK(UploadScoreSeams::ComputeCombinedFilePrioAndCredit(1.5f, 9) == doctest::Approx(135.0f));
}

TEST_CASE("Upload score seam keeps cooldown as a hard zero-score suppression while preserving base score")
{
	UploadScoreSeams::UploadScoreInputs inputs = {};
	inputs.uBaseValueMs = 100000u;
	inputs.fCreditRatio = 1.0f;
	inputs.iFilePrioNumber = 7;
	inputs.bUseCreditSystem = false;
	inputs.bApplyPriority = false;
	inputs.bApplyLowRatioBonus = true;
	inputs.uLowRatioBonus = 50u;
	inputs.bApplyLowIdDivisor = true;
	inputs.uLowIdDivisor = 2u;
	inputs.bApplyOldClientPenalty = true;
	inputs.bCooldownSuppressed = true;

	const UploadScoreSeams::UploadScoreBreakdown breakdown = UploadScoreSeams::BuildUploadScoreBreakdown(inputs);
	CHECK_EQ(breakdown.eAvailability, UploadScoreSeams::uploadScoreCooldown);
	CHECK_EQ(breakdown.uBaseScore, 50u);
	CHECK_EQ(breakdown.uEffectiveScore, 0u);
}

TEST_CASE("Upload score seam formats modifier and compact score displays consistently")
{
	UploadScoreSeams::UploadScoreInputs inputs = {};
	inputs.uBaseValueMs = 100000u;
	inputs.fCreditRatio = 1.0f;
	inputs.iFilePrioNumber = 7;
	inputs.bUseCreditSystem = false;
	inputs.bApplyPriority = false;
	inputs.bApplyLowRatioBonus = true;
	inputs.uLowRatioBonus = 50u;
	inputs.bApplyLowIdDivisor = true;
	inputs.uLowIdDivisor = 2u;

	const UploadScoreSeams::UploadScoreBreakdown breakdown = UploadScoreSeams::BuildUploadScoreBreakdown(inputs);
	CHECK(UploadScoreSeams::FormatUploadScoreModifiers(breakdown, _T("Low ratio"), _T("LowID"), _T("Cooldown")) == CString(_T("Low ratio +50, LowID /2")));
	CHECK(UploadScoreSeams::FormatEffectiveUploadScore(breakdown, _T("Low ratio"), _T("LowID"), _T("Cooldown"), _T("Friend"), _T("-")) == CString(_T("75 (Low ratio +50, LowID /2)")));
}

TEST_CASE("Upload score seam reports friend-slot displays without forcing numeric score text")
{
	UploadScoreSeams::UploadScoreBreakdown breakdown = {};
	breakdown.eAvailability = UploadScoreSeams::uploadScoreFriendSlot;
	breakdown.uEffectiveScore = 0x0FFFFFFFu;

	CHECK(UploadScoreSeams::FormatEffectiveUploadScore(breakdown, _T("Low ratio"), _T("LowID"), _T("Cooldown"), _T("Friend slot"), _T("-")) == CString(_T("Friend slot")));
}

TEST_SUITE_END;
