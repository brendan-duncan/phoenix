#pragma once

#include <QSize>
#include <QStringList>

class PhoenixView;

/// Phoenix gpu-bakeoff <document> [--out folder] [--frames a,b,c | --count n]
///                     [--width pixels] [--repeat n]
///
/// Draws sample frames of the open document every way the stage could be
/// drawn: on the CPU with QPainter (and at 4x, as the reference), and on
/// the GPU with Rive through Dawn, once at the stage's size and once at
/// twice it scaled down. Writes each picture, its difference from
/// the reference and a report (report.json) to the folder,
/// and prints the times and errors. Returns 0 when it ran.
int runGpuBakeoff(PhoenixView& view, const QSize& documentSize, int frameCount, const QStringList& options);
