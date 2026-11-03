// kontrola: C++ vyhodnotenie stromov = LightGBM v Pythone. g++ -I src/automotive/model/Applications xgregorm_gbm_check.cc
#include "xgregorm-gbm.h"
#include <cmath>
#include <iostream>
int main (int, char **v)
{
  std::ifstream h (v[1]);
  std::string a, n;
  size_t c, t;
  h >> a >> c >> t >> n;
  xggbm::Model m;
  std::string err = m.load (v[1], n);
  if (!err.empty ()) { std::cout << err << "\n"; return 1; }
  std::ifstream f (v[2]);
  std::vector<double> x (c + 1);
  size_t rows = 0;
  double worst = 0;
  while (true)
    {
      for (double &d : x) f >> d;
      if (!f) break;
      worst = std::max (worst, std::fabs (m.predict (x.data ()) - x[c]));
      rows++;
    }
  std::cout << "rows " << rows << " worst abs diff " << worst << "\n";
}
