#pragma once

namespace tu::procedures::detail {

// TU 1.1.4.10 defines an open YALK input by its analog voltage only.
inline bool yalkOpenCircuitIsNormal(double yalkVolts)
{
    return yalkVolts < 0.0;
}

} // namespace tu::procedures::detail
