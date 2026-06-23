// Part of the Carbon Language project, under the Apache License v2.0 with LLVM
// Exceptions. See /LICENSE for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception

#include "toolchain/base/real.h"

#include <gmock/gmock.h>
#include <gtest/gtest.h>

#include <optional>

#include "common/raw_string_ostream.h"

namespace Carbon::Testing {
namespace {

static auto APIntToString(const llvm::APInt& val, bool is_signed = true)
    -> std::string {
  llvm::SmallVector<char, 32> str;
  val.toString(str, 10, is_signed);
  return std::string(str.begin(), str.end());
}

enum class Op { Add, Sub, Mul, Negate };

struct RealTestParams {
  Op op;
  Real lhs;
  std::optional<Real> rhs;
  llvm::APInt expected_mantissa;
  int64_t expected_exponent;
  bool expected_is_decimal;
};

class RealTest : public ::testing::TestWithParam<RealTestParams> {};

TEST_P(RealTest, EvaluatesCorrectly) {
  const auto& params = GetParam();
  Real result;
  switch (params.op) {
    case Op::Add:
      ASSERT_TRUE(params.rhs.has_value());
      result = params.lhs + *params.rhs;
      break;
    case Op::Sub:
      ASSERT_TRUE(params.rhs.has_value());
      result = params.lhs - *params.rhs;
      break;
    case Op::Mul:
      ASSERT_TRUE(params.rhs.has_value());
      result = params.lhs * *params.rhs;
      break;
    case Op::Negate:
      result = -params.lhs;
      break;
  }

  ASSERT_EQ(result.mantissa().getBitWidth(),
            params.expected_mantissa.getBitWidth())
      << "Bit width mismatch: result has " << result.mantissa().getBitWidth()
      << ", expected " << params.expected_mantissa.getBitWidth();

  EXPECT_EQ(result.mantissa(), params.expected_mantissa)
      << "Mantissa value mismatch: result has "
      << APIntToString(result.mantissa(), true) << ", expected "
      << APIntToString(params.expected_mantissa, true);

  EXPECT_EQ(result.exponent(), params.expected_exponent)
      << "Exponent mismatch: result has " << result.exponent() << ", expected "
      << params.expected_exponent;

  EXPECT_EQ(result.is_decimal(), params.expected_is_decimal)
      << "is_decimal mismatch: result is " << result.is_decimal()
      << ", expected " << params.expected_is_decimal;
}

static auto MakeDyadicReal(int64_t mantissa, int64_t exponent) -> Real {
  return Real(llvm::APInt(64, mantissa), llvm::APInt(64, exponent),
              /*is_decimal=*/false);
}

static auto MakeDecadicReal(int64_t mantissa, int64_t exponent) -> Real {
  return Real(llvm::APInt(64, mantissa), llvm::APInt(64, exponent),
              /*is_decimal=*/true);
}

static auto MakeReal(int64_t value) -> Real {
  return Real(llvm::APInt(64, value), llvm::APInt(64, 0), /*is_decimal=*/true);
}

INSTANTIATE_TEST_SUITE_P(
    RealTests, RealTest,
    ::testing::Values(
        // Unary Negation
        RealTestParams{Op::Negate, MakeDecadicReal(12, 3), std::nullopt,
                       llvm::APInt(64, -12, true), 3, true},
        RealTestParams{Op::Negate, MakeDecadicReal(-12, 3), std::nullopt,
                       llvm::APInt(64, 12, true), 3, true},

        // Basic Addition
        RealTestParams{Op::Add, MakeReal(12000), MakeReal(500),
                       llvm::APInt(64, 125, true), 2, true},

        // Mixed Binary and Decimal Addition (2.5 + 1.5 = 4.0)
        RealTestParams{Op::Add, MakeDyadicReal(5, -1), MakeDecadicReal(15, -1),
                       llvm::APInt(64, 4, true), 0, true},

        // Subtraction
        RealTestParams{Op::Sub, MakeDecadicReal(12, 3), MakeDecadicReal(5, 2),
                       llvm::APInt(64, 115, true), 2, true},

        // Multiplication
        RealTestParams{Op::Mul, MakeDecadicReal(12, 3), MakeDecadicReal(5, 2),
                       llvm::APInt(64, 6, true), 6, true},

        // Mixed Binary and Decimal Multiplication (2.5 * 1.5 = 3.75)
        RealTestParams{Op::Mul, MakeDyadicReal(5, -1), MakeDecadicReal(15, -1),
                       llvm::APInt(64, 375, true), -2, true},

        // Edge Case 1: Negative addition overflow (underflow)
        RealTestParams{Op::Add,
                       Real(llvm::APInt::getSignedMinValue(64), 0, true),
                       Real(llvm::APInt(64, -1, true), 0, true),
                       llvm::APInt(128, "-9223372036854775809", 10), 0, true},

        // Edge Case 2: Positive addition overflow
        RealTestParams{Op::Add,
                       Real(llvm::APInt::getSignedMaxValue(64), 0, true),
                       Real(llvm::APInt(64, 1, true), 0, true),
                       llvm::APInt(128, "9223372036854775808", 10), 0, true},

        // Edge Case 3: Varying bit widths (Addition)
        RealTestParams{Op::Add, Real(llvm::APInt(64, 12000, true), 0, true),
                       Real(llvm::APInt(128, 500, true), 0, true),
                       llvm::APInt(64, 125, true), 2, true},

        // Edge Case 4: Multiplication with different bit widths and signs
        RealTestParams{Op::Mul, Real(llvm::APInt(64, -2, true), 0, true),
                       Real(llvm::APInt(128, -3, true), 0, true),
                       llvm::APInt(64, 6, true), 0, true},

        // Edge Case 5: Multiplication with extreme values (near-overflow)
        RealTestParams{Op::Mul,
                       Real(llvm::APInt::getSignedMinValue(64), 0, true),
                       Real(llvm::APInt(64, -2, true), 0, true),
                       llvm::APInt(128, 1, true) << 64, 0, true},

        // Edge Case 6: Dyadic to Decadic promotion with negative exponent
        // (0.03125) Add 1 to trigger promotion.
        RealTestParams{Op::Add, Real(llvm::APInt(64, 1, true), -5, false),
                       Real(llvm::APInt(64, 1, true), 0, true),
                       llvm::APInt(64, 103125, true), -5, true}));

}  // namespace
}  // namespace Carbon::Testing
