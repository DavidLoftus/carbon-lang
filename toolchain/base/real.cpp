// Part of the Carbon Language project, under the Apache License v2.0 with LLVM
// Exceptions. See /LICENSE for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception

#include "toolchain/base/real.h"

#include <bit>
#include <cmath>
#include <compare>

#include "common/check.h"

namespace Carbon {

static constexpr int MinAPWidth = 64;
static constexpr int WordWidth = llvm::APInt::APINT_BITS_PER_WORD;

static auto CanonicalBitWidth(int significant_bits) -> int {
  return std::max<int>(
      MinAPWidth, ((significant_bits + WordWidth - 1) / WordWidth) * WordWidth);
}

// Shrinks value to fit the current signed bit width (rounded up to nearest word
// size).
static auto ShrinkToFit(llvm::APInt& value) -> bool {
  unsigned bit_width = value.getBitWidth();
  unsigned new_width = CanonicalBitWidth(value.getSignificantBits());
  if (new_width >= bit_width) {
    return false;
  }

  value = value.trunc(new_width);
  return true;
}

// Removes `factor` from `value` by division until it is no longer divisible by
// `factor`. `value` is updated in place, and return value is number of times
// factor was removed. Expects factor to be greater than 1.
static auto RemoveFactors(llvm::APInt& value, unsigned factor) -> unsigned {
  unsigned counter = 0;
  llvm::APInt quotient(value.getBitWidth(), 0);
  while (true) {
    int64_t remainder;
    llvm::APInt::sdivrem(value, factor, quotient, remainder);
    if (remainder != 0) {
      break;
    }
    value = std::move(quotient);
    ++counter;
  }
  return counter;
}

auto Real::ConvertToDecadic() -> void {
  if (is_decimal_) {
    return;
  }

  if (exponent_ > 0) {
    // For integer values, compute full value into mantissa. We can then reduce
    // afterwards.
    unsigned new_width = CanonicalBitWidth(mantissa_.getBitWidth() + exponent_);
    if (new_width > mantissa_.getBitWidth()) {
      mantissa_ = mantissa_.sext(new_width);
    }
    mantissa_ <<= exponent_;
    exponent_ = 0;
  } else if (exponent_ < 0) {
    unsigned new_width =
        CanonicalBitWidth(mantissa_.getSignificantBits() +
                          static_cast<int>(std::log2(5) * -exponent_) + 1);
    if (new_width > mantissa_.getBitWidth()) {
      mantissa_ = mantissa_.sext(new_width);
    }
    // m * 2^e = (m * 5^-e) * 10^e
    mantissa_ *= llvm::APIntOps::pow(llvm::APInt(mantissa_.getBitWidth(), 5),
                                     -exponent_);
  }
  is_decimal_ = true;
  Normalize();
}

auto Real::Normalize() -> void {
  if (mantissa_.isZero()) {
    exponent_ = 0;
    return;
  }

  unsigned count = RemoveFactors(mantissa_, 10);
  if (count != 0) {
    exponent_ += count;
    ShrinkToFit(mantissa_);
  }
}

// Returns true if value is 0.
static auto IsZero(const Real& value) -> bool {
  return value.mantissa().isZero();
}

// Returns true if value is normalized representation of 1.
static auto IsOneNormalized(const Real& value) -> bool {
  return value.mantissa().isOne() && value.exponent() == 0;
}

// Adds two APInts handling overflow by growing result.
static auto OverflowAdd(const llvm::APInt& lhs, const llvm::APInt& rhs)
    -> llvm::APInt {
  bool is_negative = lhs.isNegative();

  bool overflow = false;
  llvm::APInt result = lhs.sadd_ov(rhs, overflow);
  if (overflow) {
    unsigned old_width = lhs.getBitWidth();
    unsigned new_width = CanonicalBitWidth(old_width + 1);
    result = result.zext(new_width);
    if (is_negative) {
      // For positive value overflow, zero-extension is sufficient, for negative
      // overflow we need to re-apply the dropped sign bits.
      result.setBits(old_width, new_width);
    }
  }
  return result;
}

auto Real::ShiftExponentIntoMantissa(unsigned n) -> void {
  if (n == 0) {
    return;
  }

  unsigned new_bit_width = CanonicalBitWidth(
      mantissa_.getSignificantBits() + static_cast<int>(std::log2(10) * n) + 1);
  if (new_bit_width > mantissa_.getBitWidth()) {
    mantissa_ = mantissa_.sext(new_bit_width);
  }

  // If we decrease exponent by n then we multiply mantissa by 10^n.
  mantissa_ *= llvm::APIntOps::pow(llvm::APInt(mantissa_.getBitWidth(), 10), n);
  exponent_ -= n;
}

auto Real::operator+=(const Real& rhs) -> Real& {
  // Quick exit for trivial case.
  if (IsZero(*this)) {
    *this = rhs;
    return *this;
  }
  if (IsZero(rhs)) {
    return *this;
  }
  // To simplify logic ensure both sides are using decadic representation.
  ConvertToDecadic();
  Real dec_rhs = rhs;
  dec_rhs.ConvertToDecadic();

  // Ensure exponents match.
  if (exponent() > dec_rhs.exponent()) {
    ShiftExponentIntoMantissa(exponent() - dec_rhs.exponent());
  } else if (exponent() < dec_rhs.exponent()) {
    dec_rhs.ShiftExponentIntoMantissa(dec_rhs.exponent() - exponent());
  }

  // Ensure bit-widths match.
  if (mantissa_.getBitWidth() < dec_rhs.mantissa_.getBitWidth()) {
    mantissa_ = mantissa_.sext(dec_rhs.mantissa_.getBitWidth());
  } else if (dec_rhs.mantissa_.getBitWidth() < mantissa_.getBitWidth()) {
    dec_rhs.mantissa_ = dec_rhs.mantissa_.sext(mantissa_.getBitWidth());
  }

  mantissa_ = OverflowAdd(mantissa_, dec_rhs.mantissa_);
  Normalize();
  return *this;
}

auto Real::operator-=(const Real& rhs) -> Real& {
  *this += (-rhs);
  return *this;
}

auto Real::operator*=(const Real& rhs) -> Real& {
  // Quick exit for trivial case.
  if (IsZero(*this)) {
    return *this;
  }
  if (IsZero(rhs)) {
    *this = Real();
    return *this;
  }
  // To simplify logic ensure both sides are using decadic.
  ConvertToDecadic();
  Real dec_rhs = rhs;
  dec_rhs.ConvertToDecadic();

  // We check if either side is 1 after normalizing.
  if (IsOneNormalized(dec_rhs)) {
    return *this;
  }
  if (IsOneNormalized(*this)) {
    *this = std::move(dec_rhs);
    return *this;
  }

  // Calculate exact bit width needed by inspecting active bits.
  unsigned bit_width =
      CanonicalBitWidth(mantissa_.getSignificantBits() +
                        dec_rhs.mantissa_.getSignificantBits() - 1);
  if (mantissa_.getBitWidth() != bit_width) {
    mantissa_ = mantissa_.sextOrTrunc(bit_width);
  }
  llvm::APInt rhs_mantissa = dec_rhs.mantissa_;
  if (rhs_mantissa.getBitWidth() != bit_width) {
    rhs_mantissa = rhs_mantissa.sextOrTrunc(bit_width);
  }

  bool overflow = false;
  mantissa_ = mantissa_.smul_ov(rhs_mantissa, overflow);
  CARBON_CHECK(!overflow, "Unexpected overflow of real");
  exponent_ = exponent_ + dec_rhs.exponent_;

  Normalize();
  return *this;
}

auto operator-(Real real) -> Real {
  real.mantissa_ = -real.mantissa_;
  return real;
}

auto operator+(Real lhs, const Real& rhs) -> Real {
  lhs += rhs;
  return lhs;
}

auto operator-(Real lhs, const Real& rhs) -> Real {
  lhs -= rhs;
  return lhs;
}

auto operator*(Real lhs, const Real& rhs) -> Real {
  lhs *= rhs;
  return lhs;
}

}  // namespace Carbon
