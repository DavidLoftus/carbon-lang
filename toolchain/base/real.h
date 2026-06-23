// Part of the Carbon Language project, under the Apache License v2.0 with LLVM
// Exceptions. See /LICENSE for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception

#ifndef CARBON_TOOLCHAIN_BASE_REAL_H_
#define CARBON_TOOLCHAIN_BASE_REAL_H_

#include <cstdint>
#include <limits>

#include "common/ostream.h"
#include "llvm/ADT/APInt.h"
#include "llvm/Support/raw_ostream.h"

namespace Carbon {

// The value of a real literal token.
//
// This is either a dyadic fraction (mantissa * 2^exponent) or a decadic
// fraction (mantissa * 10^exponent).
//
// These values are not canonicalized, because we don't expect them to repeat.
// We use RealIds in SemIR::FloatLiteralValues, and this results in all real
// literals being distinct constants, even if they represent the same value.
// TODO: Address this by using a different representation in SemIR.
class Real : public Printable<Real> {
 public:
  Real() : mantissa_(64, 0), exponent_(0), is_decimal_(true) {}

  Real(llvm::APInt mantissa, llvm::APInt exponent, bool is_decimal)
      : mantissa_(std::move(mantissa)),
        exponent_(exponent.getSignificantBits() > 32
                      ? (exponent.isNegative()
                             ? std::numeric_limits<int32_t>::min()
                             : std::numeric_limits<int32_t>::max())
                      : static_cast<int32_t>(exponent.getSExtValue())),
        is_decimal_(is_decimal) {}

  Real(llvm::APInt mantissa, int32_t exponent, bool is_decimal)
      : mantissa_(std::move(mantissa)),
        exponent_(exponent),
        is_decimal_(is_decimal) {}

  auto mantissa() const -> const llvm::APInt& { return mantissa_; }
  auto exponent() const -> int32_t { return exponent_; }
  auto is_decimal() const -> bool { return is_decimal_; }

  auto operator+=(const Real& rhs) -> Real&;
  auto operator-=(const Real& rhs) -> Real&;
  auto operator*=(const Real& rhs) -> Real&;

  friend auto operator-(Real real) -> Real;

  auto Print(llvm::raw_ostream& output_stream) const -> void {
    output_stream << mantissa_ << "*" << (is_decimal_ ? "10" : "2") << "^"
                  << exponent_;
  }

 private:
  // Converts `this` to normalized decadic representation.
  auto ConvertToDecadic() -> void;

  // Reduces `this` to most compact representation while preserving base. i.e.
  // ensures mantissa is not a multiple of the base.
  auto Normalize() -> void;

  // Updates `this` to equivalent repsentation where exponent is reduced by n.
  // Assumes bit width is already large enough.
  auto ShiftExponentIntoMantissa(unsigned n) -> void;

  // The mantissa, represented as a signed integer.
  llvm::APInt mantissa_;

  // The exponent, represented as a signed integer.
  int32_t exponent_;

  // If false, the value is mantissa * 2^exponent.
  // If true, the value is mantissa * 10^exponent.
  bool is_decimal_;
};

auto operator+(Real lhs, const Real& rhs) -> Real;
auto operator-(Real lhs, const Real& rhs) -> Real;
auto operator*(Real lhs, const Real& rhs) -> Real;

}  // namespace Carbon

#endif  // CARBON_TOOLCHAIN_BASE_REAL_H_
