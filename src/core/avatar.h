// Where her face is in a frame, for the chat box's avatar. Platform independent: the shells
// hand over the alpha channel of her idle frame and crop what comes back.
#pragma once
#include <cstddef>
#include <cstdint>

namespace pet {

// The alpha channel of a picture: the alpha of (x, y) is alpha[y * stride + x * step].
struct AlphaMask {
  int w = 0, h = 0;
  const uint8_t* alpha = nullptr;
  size_t stride = 0, step = 4;
};

// A square of the picture, in its pixels: top-left corner and side.
struct FaceBox {
  double x = 0, y = 0, side = 1;
};

// painting = false: a chibi standing from row headTopY (top of the head) down to row groundY.
//   Her face is a third of the way down the character and about half of it tall; the square
//   is centred on the longest opaque run of its middle row, so props beside her do not pull it.
// painting = true: a bust shot. The figure starts at the first rows that are more than thin
//   strands (hair, a blade, an umbrella's tip); the square is 40% of the figure tall from there,
//   centred on the figure's opaque pixels in its top part.
// hint: 3 values or nullptr. The face's centre x, centre y and side as fractions of the opaque
//   box of the picture (x by its width, y and side by its height), from assets/faces.ini; it
//   replaces the rule above.
FaceBox faceBox(const AlphaMask& m, bool painting, double headTopY, double groundY, const double* hint);

}  // namespace pet
