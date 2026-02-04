int complex_branch(int x, int y) {
  int result = 0;

  if (x > 0) {
    if (y > 10) {
      result = x + y;
    } else {
      result = x - y;
    }
  } else {
    if (y < 0) {
      result = x * 2;
    } else {
      if (x < -5) {
        result = y * 3;
      } else {
        result = x + y + 10;
      }
    }
  }

  return result;
}
