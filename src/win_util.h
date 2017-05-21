#pragma once

inline int Width(const RECT &r)
{
  return r.right - r.left;
}

inline int Height(const RECT &r)
{
  return r.bottom - r.top;
}

