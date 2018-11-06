#include "AmpGen/Wigner.h"
#include "AmpGen/Simplify.h"

using namespace AmpGen;
using namespace AmpGen::fcn;

double fact( const double& z )
{
  double f=1;
  for( int i=1;i<=z;++i) f*=i;
  return f;
}

double nCr_( const int& n, const int& r ){
  double z=1;
  for( int f=1; f <= r ; ++f ) z *= double(n+1-f)/double(f);
  return z;
}

std::string fermis( const double& s )
{
  if( int(2*s) % 2 == 0 ) return std::to_string( (int)s);
  else return std::to_string( (int)(2*s) ) + "/2";
}

Expression ExpandedBinomial( const Expression& x, const unsigned int& n )
{
  Expression sum;
  for( unsigned int k = 0 ; k <= n ; ++k ) sum = sum + nCr_(n,k) * fcn::fpow(x,k);
  return sum; 
}

Expression AmpGen::wigner_d( const Expression& cb, const double& j, const double& m, const double& n )
{
  int k_min = std::max(0.,m+n);
  int k_max = std::min(j+m,j+n);
  Expression sum = 0 ; 
  double w2_num = fact(j+m) * fact(j-m) * fact(j+n) * fact(j-n);
  double nc_intpart = 0;
  double ns_intpart = 0; 
  double frac_nc = modf( k_min -(m+n)/2.    , &nc_intpart );
  double frac_ns = modf( j + (m+n)/2. -k_min, &ns_intpart );
  Expression fractional_part = 1 ; 
  if( frac_nc == 0.5 && frac_ns != 0.5 )      fractional_part = fcn::sqrt(1+cb);
  else if( frac_nc != 0.5 && frac_ns == 0.5 ) fractional_part = fcn::sqrt(1-cb);
  else if( frac_nc == 0.5 && frac_ns == 0.5 ) fractional_part = fcn::sqrt(1-cb*cb);  
  for( double k = k_min; k <= k_max ; ++k )
  {
    double w_den  = fact(k) * fact(j+m-k)*fact(j+n-k) * fact(k-m-n);
    double norm = pow(-1,k) * sqrt(w2_num)/( w_den * pow(2,j) );
    Expression p1 = ExpandedBinomial( cb, int( k - (m+n)/2.)    );
    Expression p2 = ExpandedBinomial(-cb, int( j + (m+n)/2. - k)); 
    sum = sum + norm * p1 * p2;  
  }
  auto simplified = NormalOrderedExpression(sum);
  return pow(-1.,j+m) * fractional_part * simplified; 
}

double AmpGen::CG( 
    const double& j1,
    const double& m1,
    const double& j2, 
    const double& m2,
    const double& J,
    const double& M )
{
  if( m1+m2!=M ) return 0;
  double f1 = (2*J+1)*fact(J+j1-j2)*fact(J-j1+j2)*fact(j1+j2-J) ;
  double f2 = fact(j1+m1)*fact(j1-m1)*fact(j2+m2)*fact(j2-m2)*fact(J+M)*fact(J-M);

  double norm = f1 * f2 / fact(J+j1+j2+1) ;
  double sum  = 0;

  for( int nu=0; nu <= j1+j2-J ; ++nu){
    double arg1 = j1+j2-J-double(nu);
    double arg2 = j1  -m1-double(nu);
    double arg3 = j2+  m2-double(nu);
    double arg4 = J-j2+m1+double(nu);
    double arg5 = J-j1-m2+double(nu);
    if( arg1 < 0 || arg2 < 0 || arg3 < 0 || arg4 < 0 || arg5 < 0 ) continue ; 
    int sgn = nu % 2 == 0 ? 1 : -1;
    double to_add =  sgn / (fact(nu)*fact(arg1)*fact(arg2)*fact(arg3)*fact(arg4)*fact(arg5) );
    sum = sum + to_add ; 
  }
  return sqrt(norm) * sum ; 
}

Tensor AmpGen::rotationMatrix( const Tensor& P , const bool& handleZero ){
  if( P.dims() != std::vector<size_t>({4}) && P.dims() != std::vector<size_t>({3}) ){
    ERROR("rotationMatrix only implemented for spatial three/four vectors, rank of argument = " << 
        P.dimString() );
    return Tensor();
  } 
  Tensor R(std::vector<size_t>({P.size(),P.size()}));
  auto px = P[0];
  auto py = P[1];
  auto pz = P[2];
  auto p2  = make_cse(px*px + py*py + pz*pz);
  auto pt2 = make_cse(px*px + py*py);
  auto ip  = isqrt(p2);
  auto ipt = isqrt(pt2); 
  Expression f    = !handleZero ? pz*ip - 1 : Ternary( fcn::abs(p2 ) < 1e-6  , 0 , pz*ip - 1);
  Expression pxn  = !handleZero ? px*ipt    : Ternary( fcn::abs(pt2) < 1e-6  , 0 , px*ipt);
  Expression pyn  = !handleZero ? py*ipt    : Ternary( fcn::abs(pt2) < 1e-6  , 0 , py*ipt);
  Expression pxn2 = !handleZero ? px*ip     : Ternary( fcn::abs(px)  < 1e-6  , 0 , px*ip);
  Expression pyn2 = !handleZero ? py*ip     : Ternary( fcn::abs(py)  < 1e-6  , 0 , py*ip); 
  R(0,0) = 1. + pxn*pxn*f;
  R(1,0) = pxn*pyn*f;
  R(2,0) = pxn2;
  R(0,1) = pxn*pyn*f;
  R(1,1) = 1. + pyn*pyn*f;
  R(2,1) = pyn2;
  R(0,2) = -pxn2;
  R(1,2) = -pyn2;
  R(2,2) = 1+f; 
  R(3,3) = 1.0;
  return R;
}

Tensor AmpGen::helicityTransformMatrix( const Tensor& P, 
                                        const Expression& M, 
                                        const int& ve,
                                        const bool& handleZero )
{
  auto dim = std::vector<size_t>({4,4});
  if( P.dims() != std::vector<size_t>({4}) ){
    ERROR("rotationMatrix only implemented for spatial four vectors, rank of argument = " << P.dimString() );
    return Tensor();
  }
  if( ve != -1 && ve != +1 ) {
    ERROR(" ve indicates the sign of the transformation, cannot be ve = " << ve );
    return Tensor();
  }
  Tensor L(dim);
  auto vP       = [](auto& tensor){ return Tensor( {-tensor[0],-tensor[1],-tensor[2], tensor[3] }  ); };
  Tensor R = rotationMatrix( ve == 1 ? P : vP(P) , handleZero);
  Tensor::Index a,b,c;
  if( ve == -1 ){
    Tensor Rx(dim);
    Rx(0,0) =  1.0;
    Rx(1,1) = -1.0;
    Rx(2,2) = -1.0;
    Rx(3,3) =  1.0;
    R = Rx(a,b) * R(b,c);
  }
  if( is<Constant>(M) && std::real(M()) == 0 ) return R ; 
  else {
    Expression p = sqrt( make_cse( P[0]*P[0] + P[1]*P[1] + P[2]*P[2] ) );
    Expression E = P[3];
    L(0,0) = 1.;
    L(1,1) = 1.;
    L(2,2) =  E / M;
    L(2,3) =  - p / M;
    L(3,2) =  - p / M;
    L(3,3) =  E / M;
  }
  return L(a,b) * R(b,c);
}

Expression AmpGen::wigner_D(const Tensor& P, 
                            const double& J, 
                            const double& lA, 
                            const double& lB, 
                            const double& lC, 
                            DebugSymbols* db )
{
  Expression pz = make_cse( P[2] / sqrt( P[0]*P[0] + P[1] * P[1] + P[2]*P[2] ) );  
  Expression pt2 = make_cse( P[0]*P[0] + P[1]*P[1] );
  Expression px = P[0] / sqrt( pt2 );
  Expression py = P[1] / sqrt( pt2 );

  Expression I(std::complex<double>(0,1));
  auto little_d = make_cse ( wigner_d( pz, J, lA, lB-lC ) );
  if( J != 0 && db != nullptr ){
    db->emplace_back("ϕ"     , atan2( py, px ) );
    db->emplace_back("cosθ", pz );
    db->emplace_back("d[" + fermis(J) +", " + fermis(lA) + ", " + fermis(lB-lC) +"](cosθ)", little_d );
  }
  return  fpow( px - I * py, ( lB-lC-lA ) ) * little_d; 
}

struct LS {
  double factor;
  double cg1;
  double cg2;
  double p; 
  double m1;
  double m2;
};

std::vector<LS> calculate_recoupling_constants( 
    const double& J, 
    const double& M,
    const double& L, 
    const double& S,
    const double& j1,
    const double& j2 ){

  std::vector<LS> rt;
  for( double m1 = -j1; m1 <= j1; ++m1 ){
    for( double m2 = -j2; m2 <= j2; ++m2 ){
      LS f; 
      f.m1 = m1;
      f.m2 = m2;
      f.factor = sqrt( (2.*L + 1. )/( 2.*J + 1. ) );
      f.cg1    = AmpGen::CG(L ,0 ,S ,m1-m2,J,m1-m2);
      f.cg2    = AmpGen::CG(j1,m1,j2,-m2  ,S,m1-m2); 
      f.p      = sqrt( (2*L + 1 )/(2*J+1) );
      f.factor *= f.cg1 * f.cg2;
      if( f.factor != 0 ) rt.push_back(f);
    }
  }
  return rt;
}

Expression AmpGen::helicityAmplitude( const Particle& particle, const Tensor& parentFrame, const double& Mz, DebugSymbols* db )
{
  if( particle.daughters().size() != 2 ) return 1; 
  auto simplifiedParentFrame = particle.spin() == 0. ? Identity(4) : parentFrame;
  auto particle_couplings = particle.spinOrbitCouplings(false);
  auto L = particle.orbital();
  auto& d0 = *particle.daughter(0);
  auto& d1 = *particle.daughter(1);
  
  double S = 999;
  if( particle.S() == 0 ){ 
    for( auto& l : particle_couplings ) if( l.first == L ){ S = l.second ; break; }
    if( S == 999 ) ERROR("Spin orbital coupling impossible!");
  }
  else S = particle.S() /2.;
 
  INFO( particle.uniqueString() << " -> " << L << "  " << S << " P[S = " << particle.S() << "]" );

  auto recoupling_constants = calculate_recoupling_constants( particle.spin(), Mz, L, S, d0.spin(), d1.spin() );
  
  Expression total = 0 ; 
  Tensor::Index a,b,c;
  Tensor f1 = simplifiedParentFrame(a,b) * d0.P()(b);
  Tensor f2 = simplifiedParentFrame(a,b) * d1.P()(b);
  auto L1 = helicityTransformMatrix( f1, fcn::sqrt( d0.massSq() ), 1  , false);
  auto L2 = helicityTransformMatrix( f2, fcn::sqrt( d1.massSq() ), -1 , false);
  f1.st();
  f2.st();
  L1.st();
  L2.st();
  if( recoupling_constants.size() == 0 ){    
    WARNING( particle.uniqueString() << " " << particle.spin() << " " << 
        particle.orbitalRange(false).first << " " << particle.orbitalRange(false).second 
        <<  " transition Mz="<< Mz << " to " << d0.spin() << " x " << d0.spin() << " cannot be coupled in (LS) = " << L << ", " << S ); 
    std::string lsStr = "";
    for( auto& ls : particle_couplings ){
      lsStr += "(" + std::to_string(int(ls.first)) + ", " + std::to_string(ls.second)  +")";
    } 
    WARNING( "--Possible (LS) combinations = " << lsStr );
  }
  for( auto& coupling : recoupling_constants ){          
    std::string dt = "d[" + particle.name()+ 
                     "]_" + fermis(particle.spin()) 
                    + "_" + fermis(Mz) 
                    + "_" + fermis(coupling.m1) 
                    + "_" + fermis(coupling.m2);
    auto term = wigner_D( f1 , particle.spin(), Mz, coupling.m1, coupling.m2,db );
    DEBUG( particle.uniqueString() << " m1=" << coupling.m1 << " m2=" << coupling.m2 << " Mz=" << particle.polState() << " m1'=" << d0.polState() << " m2'=" << d1.polState() );
    if( d0.isStable() && 2 * coupling.m1 != d0.polState() ) continue; 
    if( d1.isStable() && 2 * coupling.m2 != d1.polState() ) continue; 
    INFO( "T[" << d0.name() << "] = " << coupling.factor << " [" << coupling.p << " " << coupling.cg1 << " " << coupling.cg2 << "] x D(J=" << particle.spin() << ", m=" << Mz << ", m'=" << coupling.m1 - coupling.m2 <<")" );
    auto h1   = helicityAmplitude( d0, L1(a,b) * simplifiedParentFrame(b,c) , coupling.m1, db ) ;
    auto h2   = helicityAmplitude( d1, L2(a,b) * simplifiedParentFrame(b,c) , coupling.m2, db ) ;
    if( db != nullptr ){ 
      db->emplace_back( dt, term );
      db->emplace_back( "coupling" , coupling.factor );
    }
    total = total + coupling.factor * term * h1 * h2 ; 
  }
  return total;
}