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
	CHECK(UploadScoreSeams::FormatBaseUploadScore(breakdown, _T("Friend"), _T("-")) == CString(_T("100")));
	CHECK(UploadScoreSeams::FormatEffectiveUploadScoreValue(breakdown, _T("Friend"), _T("-")) == CString(_T("75")));
	CHECK(UploadScoreSeams::FormatUploadScoreModifiers(breakdown, _T("Low ratio"), _T("LowID"), _T("Cooldown")) == CString(_T("Low ratio +50, LowID /2")));
	CHECK(UploadScoreSeams::FormatUploadScoreCompact(breakdown, _T("Low ratio"), _T("LowID"), _T("Cooldown"), _T("Friend"), _T("-")) == CString(_T("75 (Low ratio +50, LowID /2)")));
	CHECK(UploadScoreSeams::FormatUploadScoreLowRatioDetail(breakdown, _T("-")) == CString(_T("+50")));
	CHECK(UploadScoreSeams::FormatUploadScoreLowIdDetail(breakdown, _T("-")) == CString(_T("/2")));
	CHECK(UploadScoreSeams::FormatUploadScoreCooldownDetail(breakdown, 0u, _T("-")) == CString(_T("-")));
}

TEST_CASE("Upload score seam formats plain available scores without modifier noise")
{
	UploadScoreSeams::UploadScoreInputs inputs = {};
	inputs.uBaseValueMs = 120000u;
	inputs.fCreditRatio = 1.0f;
	inputs.iFilePrioNumber = 7;
	inputs.bUseCreditSystem = false;
	inputs.bApplyPriority = false;

	const UploadScoreSeams::UploadScoreBreakdown breakdown = UploadScoreSeams::BuildUploadScoreBreakdown(inputs);
	CHECK(UploadScoreSeams::FormatBaseUploadScore(breakdown, _T("Friend"), _T("-")) == CString(_T("120")));
	CHECK(UploadScoreSeams::FormatEffectiveUploadScoreValue(breakdown, _T("Friend"), _T("-")) == CString(_T("120")));
	CHECK(UploadScoreSeams::FormatUploadScoreModifiers(breakdown, _T("Low ratio"), _T("LowID"), _T("Cooldown")) == CString(_T("-")));
	CHECK(UploadScoreSeams::FormatUploadScoreCompact(breakdown, _T("Low ratio"), _T("LowID"), _T("Cooldown"), _T("Friend"), _T("-")) == CString(_T("120")));
}

TEST_CASE("Upload score seam reports friend-slot displays without forcing numeric score text")
{
	UploadScoreSeams::UploadScoreBreakdown breakdown = {};
	breakdown.eAvailability = UploadScoreSeams::uploadScoreFriendSlot;
	breakdown.uEffectiveScore = 0x0FFFFFFFu;

	CHECK(UploadScoreSeams::FormatBaseUploadScore(breakdown, _T("Friend slot"), _T("-")) == CString(_T("Friend slot")));
	CHECK(UploadScoreSeams::FormatEffectiveUploadScoreValue(breakdown, _T("Friend slot"), _T("-")) == CString(_T("Friend slot")));
	CHECK(UploadScoreSeams::FormatUploadScoreModifiers(breakdown, _T("Low ratio"), _T("LowID"), _T("Cooldown")) == CString(_T("-")));
	CHECK(UploadScoreSeams::FormatUploadScoreCompact(breakdown, _T("Low ratio"), _T("LowID"), _T("Cooldown"), _T("Friend slot"), _T("-")) == CString(_T("Friend slot")));
	CHECK(UploadScoreSeams::FormatUploadScoreLowRatioDetail(breakdown, _T("-")) == CString(_T("-")));
	CHECK(UploadScoreSeams::FormatUploadScoreLowIdDetail(breakdown, _T("-")) == CString(_T("-")));
	CHECK(UploadScoreSeams::FormatUploadScoreCooldownDetail(breakdown, 42000u, _T("-")) == CString(_T("-")));
}

TEST_CASE("Upload score seam keeps credit and file-priority multiplication in the base and effective score path")
{
	UploadScoreSeams::UploadScoreInputs inputs = {};
	inputs.uBaseValueMs = 100000u;
	inputs.fCreditRatio = 1.5f;
	inputs.iFilePrioNumber = 8;
	inputs.bUseCreditSystem = true;
	inputs.bApplyPriority = true;

	const UploadScoreSeams::UploadScoreBreakdown breakdown = UploadScoreSeams::BuildUploadScoreBreakdown(inputs);
	CHECK_EQ(breakdown.eAvailability, UploadScoreSeams::uploadScoreAvailable);
	CHECK_EQ(breakdown.uBaseScore, 120u);
	CHECK_EQ(breakdown.uEffectiveScore, 120u);
	CHECK_FALSE(breakdown.bLowRatioApplied);
	CHECK_FALSE(breakdown.bLowIdPenaltyApplied);
	CHECK_FALSE(breakdown.bOldClientPenaltyApplied);
}

TEST_CASE("Upload score seam derives stable modifier masks and sort keys from active modifiers")
{
	UploadScoreSeams::UploadScoreInputs lowRatioOnlyInputs = {};
	lowRatioOnlyInputs.uBaseValueMs = 100000u;
	lowRatioOnlyInputs.fCreditRatio = 1.0f;
	lowRatioOnlyInputs.iFilePrioNumber = 7;
	lowRatioOnlyInputs.bApplyLowRatioBonus = true;
	lowRatioOnlyInputs.uLowRatioBonus = 25u;

	UploadScoreSeams::UploadScoreInputs lowRatioAndLowIdInputs = lowRatioOnlyInputs;
	lowRatioAndLowIdInputs.bApplyLowIdDivisor = true;
	lowRatioAndLowIdInputs.uLowIdDivisor = 2u;

	UploadScoreSeams::UploadScoreInputs cooldownInputs = lowRatioAndLowIdInputs;
	cooldownInputs.bCooldownSuppressed = true;

	const UploadScoreSeams::UploadScoreBreakdown lowRatioOnly = UploadScoreSeams::BuildUploadScoreBreakdown(lowRatioOnlyInputs);
	const UploadScoreSeams::UploadScoreBreakdown lowRatioAndLowId = UploadScoreSeams::BuildUploadScoreBreakdown(lowRatioAndLowIdInputs);
	const UploadScoreSeams::UploadScoreBreakdown cooldown = UploadScoreSeams::BuildUploadScoreBreakdown(cooldownInputs);

	CHECK_EQ(UploadScoreSeams::BuildUploadScoreModifierMask(lowRatioOnly), 2u);
	CHECK_EQ(UploadScoreSeams::BuildUploadScoreModifierMask(lowRatioAndLowId), 3u);
	CHECK_EQ(UploadScoreSeams::BuildUploadScoreModifierMask(cooldown), 7u);
	CHECK(UploadScoreSeams::BuildUploadScoreModifierSortKey(lowRatioOnly) < UploadScoreSeams::BuildUploadScoreModifierSortKey(lowRatioAndLowId));
	CHECK(UploadScoreSeams::BuildUploadScoreModifierSortKey(lowRatioAndLowId) < UploadScoreSeams::BuildUploadScoreModifierSortKey(cooldown));
}

TEST_CASE("Upload score seam reports unavailable and cooldown displays consistently across split and detail fields")
{
	UploadScoreSeams::UploadScoreBreakdown unavailable = {};
	CHECK(UploadScoreSeams::FormatBaseUploadScore(unavailable, _T("Friend slot"), _T("-")) == CString(_T("-")));
	CHECK(UploadScoreSeams::FormatEffectiveUploadScoreValue(unavailable, _T("Friend slot"), _T("-")) == CString(_T("-")));
	CHECK(UploadScoreSeams::FormatUploadScoreModifiers(unavailable, _T("Low ratio"), _T("LowID"), _T("Cooldown")) == CString(_T("-")));
	CHECK(UploadScoreSeams::FormatUploadScoreCompact(unavailable, _T("Low ratio"), _T("LowID"), _T("Cooldown"), _T("Friend slot"), _T("-")) == CString(_T("-")));
	CHECK(UploadScoreSeams::FormatUploadScoreLowRatioDetail(unavailable, _T("-")) == CString(_T("-")));
	CHECK(UploadScoreSeams::FormatUploadScoreLowIdDetail(unavailable, _T("-")) == CString(_T("-")));
	CHECK(UploadScoreSeams::FormatUploadScoreCooldownDetail(unavailable, 1500u, _T("-")) == CString(_T("-")));

	UploadScoreSeams::UploadScoreBreakdown cooldown = {};
	cooldown.eAvailability = UploadScoreSeams::uploadScoreCooldown;
	cooldown.uBaseScore = 120u;
	cooldown.uEffectiveScore = 0u;
	CHECK(UploadScoreSeams::FormatBaseUploadScore(cooldown, _T("Friend slot"), _T("-")) == CString(_T("120")));
	CHECK(UploadScoreSeams::FormatEffectiveUploadScoreValue(cooldown, _T("Friend slot"), _T("-")) == CString(_T("0")));
	CHECK(UploadScoreSeams::FormatUploadScoreModifiers(cooldown, _T("Low ratio"), _T("LowID"), _T("Cooldown")) == CString(_T("Cooldown")));
	CHECK(UploadScoreSeams::FormatUploadScoreCompact(cooldown, _T("Low ratio"), _T("LowID"), _T("Cooldown"), _T("Friend slot"), _T("-")) == CString(_T("0 (Cooldown)")));
	CHECK(UploadScoreSeams::FormatUploadScoreCooldownDetail(cooldown, 1500u, _T("-")) == CString(_T("2s")));
}

TEST_SUITE_END;
