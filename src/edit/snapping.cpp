#include "snapping.h"

#include <cmath>

namespace fla {

void Snapper::clearCandidates()
{
    _candidatesX.clear();
    _candidatesY.clear();
}

void Snapper::addCandidateX(double x)
{
    _candidatesX.push_back(x);
}

void Snapper::addCandidateY(double y)
{
    _candidatesY.push_back(y);
}

SnapResult Snapper::snap(const std::vector<double>& values, double gridSpacing,
    const std::vector<double>& candidates, const std::vector<double>& guides) const
{
    SnapResult best;
    double bestDistance = _tolerance;

    for (double value : values)
    {
        // A guide is placed on purpose, so it is considered first and holds
        // against anything no closer.
        if (_guideSnapEnabled)
        {
            for (double guide : guides)
            {
                const double distance = std::fabs(guide - value);
                if (distance <= bestDistance)
                {
                    bestDistance = distance;
                    best.adjustment = guide - value;
                    best.kind = SnapKind::Guide;
                    best.target = guide;
                }
            }
        }

        // Lining up with another object beats the grid, so objects are
        // considered first and the grid only wins if it is strictly closer.
        if (_objectSnapEnabled)
        {
            for (double candidate : candidates)
            {
                const double distance = std::fabs(candidate - value);
                if (distance < bestDistance || (distance <= bestDistance && best.kind != SnapKind::Guide))
                {
                    bestDistance = distance;
                    best.adjustment = candidate - value;
                    best.kind = SnapKind::Object;
                    best.target = candidate;
                }
            }
        }

        if (_gridEnabled && gridSpacing > 0.0)
        {
            const double target = std::round(value / gridSpacing) * gridSpacing;
            const double distance = std::fabs(target - value);
            if (distance < bestDistance)
            {
                bestDistance = distance;
                best.adjustment = target - value;
                best.kind = SnapKind::Grid;
                best.target = target;
            }
        }
    }

    return best;
}

SnapResult Snapper::snapX(const std::vector<double>& values) const
{
    return snap(values, _gridSpacingX, _candidatesX, _guidesX);
}

SnapResult Snapper::snapY(const std::vector<double>& values) const
{
    return snap(values, _gridSpacingY, _candidatesY, _guidesY);
}

} // namespace fla
