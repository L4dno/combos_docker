#include <math.h>
#include <boost/random/hyperexponential_distribution.hpp>
#include <boost/random/uniform_01.hpp>
#include <stdio.h>
#include <stdlib.h>
#include <array>
#include <ctime>
#include <concepts>

template <class T>
concept RandGeneratorC = requires(T generator) {
  {
    generator()
  } -> std::convertible_to<double>;

  {
    generator.max()
  } -> std::convertible_to<double>;
};

/*-------------  UNIFORM [0, 1) RANDOM NUMBER GENERATOR  -------------*/
template <RandGeneratorC RndGen>
double uniform(RndGen &rndg)
{
  return rndg() * 1.0 / rndg.max();
}

/*-------------  UNIFORM (0, 1) RANDOM NUMBER GENERATOR  -------------*/
template <RandGeneratorC RndGen>
double uniform_pos(RndGen &rndg)
{
  double g;

  g = uniform(rndg);
  while (g == 0.0)
    g = uniform(rndg);

  return g;
}

/*------------  UNIFORM [a, b) RANDOM VARIATE GENERATOR  -------------*/
template <RandGeneratorC RndGen>
double uniform_ab(double a, double b, RndGen &rndg)
{ /* 'uniform' returns a psuedo-random variate from a uniform     */
  /* distribution with lower bound a and upper bound b.           */
  if (a > b)
    printf("uniform Argument Error: a > b\n");
  return (a + (b - a) * uniform(rndg));
}

/*--------------------  RANDOM INTEGER GENERATOR  --------------------*/
template <RandGeneratorC RndGen>
int uniform_int(int i, int n, RndGen &rndg)
{ /* 'random' returns an integer equiprobably selected from the   */
  /* set of integers i, i+1, i+2, . . , n.                        */
  if (i > n)
    printf("random Argument Error: i > n\n");
  n -= i;
  n = (n + 1.0) * uniform(rndg);
  return (i + n);
}

/*--------------  EXPONENTIAL RANDOM VARIATE GENERATOR  --------------*/
/* The exponential distribution has the form

   p(x) dx = exp(-x/landa) dx/landa

   for x = 0 ... +infty
*/
template <RandGeneratorC RndGen>
double exponential(double landa, RndGen &rndg)
{
  /* 'exponential' returns a psuedo-random variate from a negative     */
  /* exponential distribution with mean 1/landa.                        */

  double u = uniform_pos(rndg);
  double mean = 1.0 / landa;

  return -mean * log(u);
}

/*----------------  ERLANG RANDOM VARIATE GENERATOR  -----------------*/
template <RandGeneratorC RndGen>
double erlang(double x, double s, RndGen &rndg)
{ /* 'erlang' returns a psuedo-random variate from an erlang      */
  /* distribution with mean x and standard deviation s.           */
  int i, k;
  double z;
  if (s > x)
    printf("erlang Argument Error: s > x\n");
  z = x / s;
  k = (int)z * z;
  z = 1.0;
  for (i = 0; i < k; i++)
    z *= uniform_pos(rndg);
  return (-(x / k) * log(z));
}

/*-----------  HYPEREXPONENTIAL RANDOM VARIATE GENERATION  -----------*/
template <RandGeneratorC RndGen>
double hyperx(double x, double s, RndGen &rndg)
{ /* 'hyperx' returns a psuedo-random variate from Morse's two-   */
  /* stage hyperexponential distribution with mean x and standard */
  /* deviation s, s>x.  */
  double cv, z, p;
  if (s <= x)
    printf("hyperx Argument Error: s not > x\n");
  cv = s / x;
  z = cv * cv;
  p = 0.5 * (1.0 - sqrt((z - 1.0) / (z + 1.0)));
  z = (uniform_pos(rndg) > p) ? (x / (1.0 - p)) : (x / p);
  return (-0.5 * z * log(uniform_pos(rndg)));
}

/*-----------  3-PHASE HYPEREXPONENTIAL RANDOM VARIATE GENERATION  -----------*/
template <RandGeneratorC RndGen>
double hyperx3(std::array<double, 3> x, std::array<double, 3> s, RndGen &rndg)
{ /* 'hyperx' returns a psuedo-random variate from three-   */
  /* stage hyperexponential distribution with probabilities x and rates s */
  boost::random::hyperexponential_distribution<>
      distr(x.begin(), x.end(), s.begin(), s.end());

  return distr(rndg);
}

/*-----------------  NORMAL RANDOM VARIATE GENERATOR  ----------------*/
template <RandGeneratorC RndGen>
double normal(double x, double s, RndGen &rndg)
{ /* 'normal' returns a psuedo-random variate from a normal dis-  */
  /* tribution with mean x and standard deviation s.              */
  double v1, v2, w, z1;
  static double z2 = 0.0;
  if (z2 != 0.0)
  {
    z1 = z2;
    z2 = 0.0;
  } /* use value from previous call */
  else
  {
    do
    {
      v1 = 2.0 * uniform_pos(rndg) - 1.0;
      v2 = 2.0 * uniform_pos(rndg) - 1.0;
      w = v1 * v1 + v2 * v2;
    } while (w >= 1.0);
    w = sqrt((-2.0 * log(w)) / w);
    z1 = v1 * w;
    z2 = v2 * w;
  }
  return (x + z1 * s);
}

/*---------- LOGNORMAL RANDOM VARIATE GENERATOR ------------*/
template <RandGeneratorC RndGen>
double ran_lognormal(double p, double u, RndGen &rndg)
{
  return exp(normal(p, u, rndg));
}

/* The Weibull distribution has the form,

   p(x) dx = (k/a) (x/a)^(k-1) exp(-(x/a)^k) dx

   k = shape
  a = landa = scale
 */

template <RandGeneratorC RndGen>
double ran_weibull(const double k, const double a, RndGen &rndg)
{
  double x = uniform_pos(rndg);

  double z = pow(-log(x), 1 / k);

  return a * z;
}

template <RandGeneratorC RndGen>
double gamma_large(const double a, RndGen &rndg)
{
  /* Works only if a > 1, and is most efficient if a is large

     This algorithm, reported in Knuth, is attributed to Ahrens.  A
     faster one, we are told, can be found in: J. H. Ahrens and
     U. Dieter, Computing 12 (1974) 223-246 begin_of_the_skype_highlighting              12 (1974) 223-246      end_of_the_skype_highlighting.  */

  double sqa, x, y, v;
  sqa = sqrt(2 * a - 1);
  do
  {
    do
    {
      y = tan(M_PI * uniform(rndg));
      x = sqa * y + a - 1;
    } while (x <= 0);
    v = uniform(rndg);
  } while (v > (1 + y * y) * exp((a - 1) * log(x / (a - 1)) - sqa * y));

  return x;
}

template <RandGeneratorC RndGen>
double ran_gamma_int(const unsigned int a, RndGen &rndg)
{
  if (a < 12)
  {
    unsigned int i;
    double prod = 1;

    for (i = 0; i < a; i++)
    {
      prod *= uniform_pos(rndg);
    }

    /* Note: for 12 iterations we are safe against underflow, since
       the smallest positive random number is O(2^-32). This means
       the smallest possible product is 2^(-12*32) = 10^-116 which
       is within the range of double precision. */

    return -log(prod);
  }
  else
  {
    return gamma_large((double)a, rndg);
  }
}

template <RandGeneratorC RndGen>
double gamma_frac(const double a, RndGen &rndg)
{
  /* This is exercise 16 from Knuth; see page 135, and the solution is
     on page 551.  */

  double p, q, x, u, v;
  p = M_E / (a + M_E);
  do
  {
    u = uniform(rndg);
    v = uniform_pos(rndg);

    if (u < p)
    {
      x = exp((1 / a) * log(v));
      q = exp(-x);
    }
    else
    {
      x = 1 - log(v);
      q = exp((a - 1) * log(x));
    }
  } while (uniform(rndg) >= q);

  return x;
}

/* The Gamma distribution

   k = shape
   b = teta = scale

   p(x) dx = {1 / \Gamma(k) b^a } x^{k-1} e^{-x/b} dx

   for x>0.  If X and Y are independent gamma-distributed random
   variables of order a1 and a2 with the same scale parameter b, then
   X+Y has gamma distribution of order a1+a2.

   The algorithms below are from Knuth, vol 2, 2nd ed, p. 129. */

template <RandGeneratorC RndGen>
double ran_gamma(const double k, const double b, RndGen &rndg)
{
  /* assume a > 0 */
  unsigned int na = floor(k);

  if (k == na)
  {
    return b * ran_gamma_int(na, rndg);
  }
  else if (na == 0)
  {
    return b * gamma_frac(k, rndg);
  }
  else
  {
    return b * (ran_gamma_int(na, rndg) + gamma_frac(k - na, rndg));
  }
}

template <RandGeneratorC RndGen>
double ran_distri(char num, std::vector<double> p, std::vector<double> u, RndGen &rndg)
{
  if (num == 8)
  {
    std::array<double, 3> arr_p = {p[0], p[1], p[2]};
    std::array<double, 3> arr_u = {u[0], u[1], u[2]};
    return hyperx3(arr_p, arr_u, rndg);
  }
  double a = p[0], b = u[0];
  switch (num)
  {
  case 0:
    return ran_weibull(a, b, rndg);
  case 1:
    return ran_gamma(a, b, rndg);
  case 2:
    return ran_lognormal(a, b, rndg);
  case 3:
    return normal(a, b, rndg);
  case 4:
    return hyperx(a, b, rndg);
  case 5:
    return exponential(a, rndg);
  case 6:
    return 1;
  case 7:
    return 0;

  default:
    // ERROR
    printf("You've chosen an incorrect random distribution\n");
    return -1;
  }
  return 0;
}