#pragma once

#include <QSize>
#include <QStringList>

class PhoenixView;

/// Phoenix gpu-bakeoff <document> [--out folder] [--frames a,b,c | --count n]
///                     [--width pixels] [--repeat n]
///
/// Draws sample frames of the open document every way the stage could be
/// drawn: QPainter as the stage does now (and at 4x, as the reference),
/// and each GPU vector renderer through Dawn, once at the stage's size and
/// once at twice it scaled down. Writes each picture, its difference from
/// the reference and a report (report.json) to the folder,
/// and prints the times and errors. Returns 0 when it ran.
int runGpuBakeoff(PhoenixView& view, const QSize& documentSize, int frameCount, const QStringList& options);
