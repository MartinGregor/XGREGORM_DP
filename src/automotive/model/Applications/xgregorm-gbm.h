// xgregorm 2026-11-03: vyhodnotenie naucenych rozhodovacich stromov (LightGBM, xgregorm_gbm_export.py) v aute.
// Bez zavislosti na ns-3, aby sa dalo overit samostatne proti Pythonu (xgregorm_gbm_check.cc).
#ifndef XGREGORM_GBM_H
#define XGREGORM_GBM_H
#include <fstream>
#include <sstream>
#include <string>
#include <vector>

namespace xggbm {

struct Node
{
  int f;      // priznak, -1 = list
  double thr; // x[f] <= thr -> vlavo
  int l, r;
  double val;
};

struct Model
{
  std::vector<std::vector<Node>> trees;
  std::vector<std::string> names;
  bool empty () const { return trees.empty (); }

  // vrati prazdny retazec, alebo popis chyby
  std::string
  load (const std::string &path, const std::string &expectedNames)
  {
    std::ifstream f (path);
    if (!f)
      return "cannot read " + path;
    std::string tag, nm;
    size_t nf, nt;
    f >> tag >> nf >> nt;
    if (tag != "GBM")
      return "bad header in " + path;
    f >> nm;
    if (nm != expectedNames)
      return "feature order in " + path + " is " + nm + ", expected " + expectedNames;
    trees.resize (nt);
    for (size_t t = 0; t < nt; t++)
      {
        size_t n;
        f >> tag >> n;
        if (tag != "T")
          return "bad tree in " + path;
        trees[t].resize (n);
        for (Node &o : trees[t])
          f >> o.f >> o.thr >> o.l >> o.r >> o.val;
      }
    if (!f)
      return "truncated " + path;
    return "";
  }

  double
  predict (const double *x) const
  {
    double s = 0;
    for (const std::vector<Node> &t : trees)
      {
        int i = 0;
        while (t[i].f >= 0)
          i = x[t[i].f] <= t[i].thr ? t[i].l : t[i].r;
        s += t[i].val;
      }
    return s;
  }
};

} // namespace xggbm
#endif
