#pragma once
// HANABI_POINTER_PROBE=<x>,<y>: a native mouseMoved at that window point every
// frame, and one line per frame with every coordinate the pointer passes
// through beside the hot widget's rect. See pointer_probe.mm.
namespace hanabi::pointer_probe {
bool armed();
void frame();
}  // namespace hanabi::pointer_probe
