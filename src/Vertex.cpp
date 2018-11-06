#include "AmpGen/Vertex.h"

#include <array>
#include <bitset>
#include <memory>
#include <ostream>

#include "AmpGen/DiracMatrices.h"
#include "AmpGen/NamedParameter.h"
#include "AmpGen/MsgService.h"
#include "AmpGen/Units.h"

using namespace AmpGen;
using namespace AmpGen::Dirac;
using namespace AmpGen::Vertex;

const Tensor::Index mu    = Tensor::Index();
const Tensor::Index nu    = Tensor::Index();
const Tensor::Index alpha = Tensor::Index();
const Tensor::Index beta  = Tensor::Index();

template <>
Factory<AmpGen::Vertex::VertexBase>* Factory<AmpGen::Vertex::VertexBase>::gImpl = nullptr;

bool VertexFactory::isVertex( const std::string& hash ) { return get( hash ) != nullptr; }

Tensor VertexFactory::getSpinFactor( const Tensor& P, const Tensor& Q, const Tensor& V1, const Tensor& V2,
    const std::string& name, DebugSymbols* db )
{
  auto connector = VertexFactory::get( name );
  if ( connector == nullptr ) {
    ERROR( "Could not find vertex: " << name ) ; 
    return Tensor( std::vector<double>( {1.} ), {0} );
  } else
    return (*connector)( P, Q, V1, V2, db );
}

Tensor VertexFactory::getSpinFactorNBody( const std::vector<std::pair<Tensor, Tensor>>& tensors, const unsigned int& mL,
    DebugSymbols* db )
{
  if ( tensors.size() != 3 ) {
    ERROR( "N-Body spin tensors only implemented for specific three-body decays, please check logic" );
    return Tensor( std::vector<double>( {1.} ), {1} );
  }
  if ( mL == 1 )
    return LeviCivita()( -mu, -nu, -alpha, -beta ) *
      tensors[0].first( nu ) * tensors[1].first( alpha ) * tensors[2].first( beta ) / ( GeV * GeV * GeV );
  else if ( mL == 0 )
    return Tensor( std::vector<double>( {1} ), {1} );
  else
    return Tensor( std::vector<double>( {1.} ), {1} );
}

const Tensor Metric4x4(){
  return Tensor( {-1, 0, 0, 0, 0, -1, 0, 0, 0, 0, -1, 0, 0, 0, 0, 1}, {4, 4} );
}

Tensor AmpGen::Orbital_PWave( const Tensor& P, const Tensor& Q )
{ 
  auto is = 1./make_cse( dot(P,P) ,true);
  return Q - P * make_cse( dot( P, Q ) ) * is ; // / make_cse( dot( P, P ) );
}


Tensor AmpGen::Orbital_DWave( const Tensor& P, const Tensor& Q )
{
  Tensor::Index mu;
  Tensor::Index nu;
  Tensor L = Orbital_PWave( P, Q );
  Tensor f =  L(mu) * L(nu) - make_cse( dot( L, L ) / 3. ) * Spin1Projector(P) ( mu, nu );
  f.imposeSymmetry(0,1); 
  f.st();
  return f;  
}

Tensor AmpGen::Spin1Projector( const Tensor& P )
{
  Tensor::Index mu;
  Tensor::Index nu;
  auto is = 1./make_cse( dot(P,P) , true);
  return Metric4x4()(mu, nu) - P(mu) * P(nu) * is ; // / make_cse( dot(P, P) , true ); 
}

Tensor AmpGen::Spin2Projector( const Tensor& P )
{
  Tensor::Index mu, nu, alpha, beta;
  Tensor S = Spin1Projector( P );
  Tensor SP =  -( 1. / 3. ) * S( mu, nu ) * S( alpha, beta ) + (1./2.) * ( S( mu, alpha ) * S( nu, beta ) + S( mu, beta ) * S( nu, alpha ) ) ;
  return SP;
}

Tensor AmpGen::Gamma4Vec()
{
  Tensor::Index a, b, mu;
  Tensor x( {1, 0, 0, 0}, {4} );
  Tensor y( {0, 1, 0, 0}, {4} );
  Tensor z( {0, 0, 1, 0}, {4} );
  Tensor t( {0, 0, 0, 1}, {4} );
  return x( mu ) * Gamma[0]( a, b ) + y( mu ) * Gamma[1]( a, b ) + z( mu ) * Gamma[2]( a, b ) + t( mu ) * Gamma[3]( a, b );
}

Tensor AmpGen::slash( const Tensor& P )
{
  if ( P.dims() != std::vector<size_t>({4}) ) {
    ERROR( "Can only compute slash operator against vector currents" );
    return Tensor();
  }
  Tensor::Index mu, a, b;
  Tensor rt = Gamma4Vec()( mu, a, b ) * P( -mu );
  return rt;
}

Tensor AmpGen::gamma_twiddle( const Tensor& P )
{
  Tensor::Index mu, nu, a, b ;
  Tensor g = Gamma4Vec();
  return g(nu,a,b) - P(nu) * P(mu) * g(-mu,a,b) / make_cse( dot(P,P) );
}

Tensor AmpGen::Spin1hProjector( const Tensor& P )
{
  Expression m = make_cse( fcn::sqrt( dot( P, P ) ) );
  return 0.5 * ( slash(P)/m + Identity() );
}

Tensor AmpGen::Spin3hProjector( const Tensor& P )
{
  Tensor::Index a,b,c, d, mu, nu; 
  Tensor Ps = P;
  Ps.st();
  Tensor g = gamma_twiddle(Ps);
  Tensor F = Spin1hProjector(Ps);
  Tensor S = Spin1Projector(Ps);
  S.imposeSymmetry(0,1);
  Tensor rt = (-1.) * S(mu,nu) * F(a,b) + (1./3.) * F(a,c) * g(mu,c,d) * g(nu,d,b);
  rt.st(true);
  return rt;
}

Tensor AmpGen::Bar( const Tensor& P ){
  Tensor::Index a,b;
  return P.conjugate()(b) * Gamma[3](b,a) ;
}

DEFINE_VERTEX( S_SS_S ) { return V1 * V2[0]; }

DEFINE_VERTEX( S_VV_S ) { auto v1_dot_v2 = V1( mu ) * V2( -mu ); ADD_DEBUG( v1_dot_v2,db); return v1_dot_v2; }

DEFINE_VERTEX( S_VV_D )
{
  Tensor L2   = Orbital_DWave( P, Q ) / ( GeV * GeV );
  Tensor vtol = V1( mu ) * L2( -mu, -nu ) * V2( nu );
  auto LD = Orbital_PWave(P,Q);
  ADD_DEBUG(LD[0],db);
  ADD_DEBUG(LD[1],db);
  ADD_DEBUG(LD[2],db);
  ADD_DEBUG(LD[3],db);
  return vtol;
}

DEFINE_VERTEX( S_VV_P )
{
  Tensor L        = Orbital_PWave( P, Q );
  Tensor coupling = LeviCivita()( -mu, -nu, -alpha, -beta ) * L( alpha ) * P( beta );
  return V1( mu ) * coupling( -mu, -nu ) * V2( nu ) / ( GeV * GeV );
}

DEFINE_VERTEX( S_VS_P )
{
  Tensor p_wave = Orbital_PWave( P, Q );
  Tensor p_v1   = V1( mu ) * p_wave( -mu );
  return p_v1 * V2[0] / GeV;
}

DEFINE_VERTEX( V_SS_P )
{
  Tensor p_wave          = Orbital_PWave( P, Q );
  Expression scalar_part = V1[0] * V2[0] / GeV;
  Tensor L     = p_wave * scalar_part;
  ADD_DEBUG( L[0], db ); 
  ADD_DEBUG( L[1], db ); 
  ADD_DEBUG( L[2], db ); 
  ADD_DEBUG( L[3], db ); 
  return L;
}

DEFINE_VERTEX( V_VS_P )
{
  Tensor  L        = Orbital_PWave( P, Q ) / GeV; 
  Tensor coupling = LeviCivita()( -mu, -nu, -alpha, -beta ) * L( alpha ) * P( beta ) / GeV;
  return ( coupling(mu,nu) * V1( -nu ) ) * V2[0];
}


DEFINE_VERTEX( V_VS_S )
{
  Tensor L = Spin1Projector(P)(mu,nu) * V1(-nu) * V2[0];
  return L;
}

DEFINE_VERTEX( V_VS_D )
{
  Tensor L_2_V0 = Orbital_DWave( P, Q ) / ( GeV * GeV );
  Tensor Sv     = Spin1Projector( P );
  return ( Sv( mu, nu ) * L_2_V0( -nu, -alpha ) * V1( alpha ) ) * V2[0];
}

DEFINE_VERTEX( T_VS_D )
{
  Tensor G = LeviCivita()( -mu, -nu, -alpha, -beta ) * P( nu ) * Q( alpha ) * V1( beta );
  Tensor L = Orbital_PWave( P, Q );
  return ( G( mu ) * L( nu ) + L( mu ) * G( nu ) ) * V2[0] / Constant( -2 * GeV * GeV * GeV );
}

DEFINE_VERTEX( T_TS_S )
{
  Tensor S     = Spin1Projector( P );
  Tensor term1 = S( -mu, -alpha ) * V1( alpha, beta ) * S( -beta, -nu );
  Tensor term2 = S * ( dot( V1, S ) ) / 3.;
  return ( term1 - term2 ) * V2[0];
}

DEFINE_VERTEX( T_VS_P )
{
  Tensor L  = Orbital_PWave( P, Q ) / GeV;
  Tensor S  = Spin1Projector( P );
  Tensor Vp = S( -mu, -nu ) * V1( nu );
  return V2[0] * ( ( L( alpha ) * Vp( beta ) + L( beta ) * Vp( alpha ) ) / 2. - S( alpha, beta ) * dot( L, V1 ) / 3. );
}

DEFINE_VERTEX( T_SS_D ) { return Orbital_DWave( P, Q )  * V1[0] * V2[0] / ( GeV * GeV ); }

DEFINE_VERTEX( S_TV_P )
{
  Tensor L = Orbital_PWave( P, Q ) / GeV;
  return ( V1( mu, nu ) * L( -mu ) ) * V2( -nu );
}

DEFINE_VERTEX( S_TS_D )
{
  Tensor orbital = Orbital_DWave( P, Q );
  return V2[0] * Tensor( {dot( orbital, V1 ) / ( GeV * GeV )}, {1} );
}

DEFINE_VERTEX( S_TV_D )
{
  Tensor term1 = V1( alpha, beta ) * Orbital_DWave( P, Q )( -beta, -nu );
  Tensor term2 = LeviCivita()( -mu, -nu, -alpha, -beta ) * P( alpha ) * V2( beta ); // Antisymmetric( P, V2 );
  return Tensor( {dot( term1, term2 )} ) / ( GeV * GeV * GeV );
}

DEFINE_VERTEX( S_TT_S ) { return Tensor( {dot( V1, V2 )} ); }

DEFINE_VERTEX( V_TS_P )
{
  Tensor S = Spin1Projector( P );
  Tensor L = Orbital_PWave( P, Q ) / ( GeV );
  return ( S( -mu, -nu ) * L( -alpha ) * V1( nu, alpha ) ) * V2[0];
}

DEFINE_VERTEX( V_TS_D )
{
  Tensor L        = ( -1 ) * Orbital_PWave( P, Q );
  Tensor coupling = LeviCivita()( -mu, -nu, -alpha, -beta ) * P( nu ) * Q( alpha );
  return coupling( -mu, -nu ) * V1( nu, alpha ) * L( -alpha ) / ( GeV * GeV * GeV );
}

DEFINE_VERTEX( f_fS_S )
{
  Tensor::Index a,b;
  return Spin1hProjector(P)(a,b) * V1(b) * V2[0];
}

DEFINE_VERTEX( f_fS_S1 )
{
  Tensor::Index a,b,c;
  return Spin1hProjector(P)(a,b) * Gamma[4](b,c) * V1(c) * V2[0];
}

DEFINE_VERTEX( f_fS_P )
{
  Tensor::Index a,b,c;
  return Spin1hProjector(P)(a, b) * slash(Orbital_PWave(P,Q))(b,c) * V1(c) * V2[0] / GeV;
}

DEFINE_VERTEX( f_fS_P1 )
{
  Tensor::Index a,b,c,d;
  return Spin1hProjector(P)(a, b) * Gamma[4](b,c) * slash(Orbital_PWave(P,Q))(c,d) * V1(d) * V2[0];
}

DEFINE_VERTEX( f_Vf_S )
{
  Tensor::Index a,b;
  Tensor proj   = Spin1hProjector(P);
  Tensor vSlash = gamma_twiddle(P)(mu,a,b) * V1(-mu);
  Tensor decaySpinor = Gamma[4](beta,nu) * vSlash( nu, mu ) * V2(mu);
  return proj(alpha,beta) * decaySpinor(beta);
}

DEFINE_VERTEX( f_Vf_S1 )
{
  Tensor proj   = Spin1hProjector(P);
  Tensor::Index a,b;
  Tensor vSlash = gamma_twiddle(P)(mu,a,b) * V1(-mu);
  Tensor decaySpinor = vSlash( a, b ) * V2(b); 
  return proj( a, b ) * decaySpinor(b);
}

DEFINE_VERTEX( f_Vf_P )
{
  Tensor proj   = Spin1hProjector(P);
  return proj( alpha, beta ) * V2(beta) * dot( V1, Orbital_PWave( P, Q ) ) / GeV ;
}

DEFINE_VERTEX( f_Tf_P )
{
  Tensor::Index a,b,c;
  Tensor proj   = Spin1hProjector(P);
  Tensor T = V1;
  T.imposeSymmetry(0,1);
  T.st();
  return proj( a, b ) * gamma_twiddle(P)(mu,b,c) * V2(c) * T(-mu,-nu) * Orbital_PWave(P,Q)(nu) / GeV;
}

DEFINE_VERTEX( f_Vf_P1 )
{
  Tensor proj   = Spin1hProjector(P);
  return proj( alpha, beta ) * Gamma[4](beta,nu) * V2(nu) * dot( V1, Orbital_PWave( P, Q ) ) / GeV;
}

DEFINE_VERTEX( f_Vf_D )
{
  Tensor::Index a,b,c,d;
  Tensor proj   = Spin1hProjector(P);
  Tensor gt     = gamma_twiddle(P);
  Tensor L      = Orbital_DWave(P,Q)(-mu,-nu) * V1(nu) / (GeV*GeV);
  return proj( a, b ) * Gamma[4](b,c) * gt(mu,c,d) * V2(d) * L(-mu);
}

DEFINE_VERTEX( r_fS_P )
{
  Tensor::Index a,b,c,d;
  Tensor L = Orbital_PWave(P,Q);
  L.st();
  if( NamedParameter<bool>("UseSimplifiedVertex", true ) ){
    Tensor F = Spin1hProjector(P);
    Tensor V = ( (-1) * L(nu) * F(a,d) + (1./3.) * F(a,b) * gamma_twiddle(P)(nu,b,c) * slash(L)(c,d) ) * V1(d) / GeV; 
    V.st();
    return V;
  }
  
  Tensor sp = Spin3hProjector(P); 
  Tensor rt = sp(mu,nu,a,b) * L(-nu) * V1(b) / (GeV);
  rt.st();   
  return rt;
}

DEFINE_VERTEX( r_fS_D )
{
  Tensor::Index a,b,c,d,e;
  Tensor sp = Spin3hProjector(P); 
  if( NamedParameter<bool>("UseSimplifiedVertex", true ) ){
    Tensor L = Orbital_PWave(P,Q);
    Tensor F = Spin1hProjector(P);
    L.st(1);
    Expression L2 = make_cse(dot(L,L));
    Tensor gt = gamma_twiddle(P);
    Tensor rt =  ( L(mu) * F(a,b) * slash(L)(b,c) - (L2/3.) * F(a,b) * gt(mu,b,c) ) * Gamma[4](c,d) * V1(d) / (GeV*GeV); 
    rt.st();
    return rt;
  } 
  Tensor  L = Orbital_DWave(P,Q);
  L.st();
  Tensor psi = gamma_twiddle(P)(mu,a,b) * Gamma[4](b,c) * L(-mu,-nu) * V1(c) / (GeV*GeV) ; 
  Tensor rt = (-1) * ( sp(mu,nu,a,b) * psi(b,-nu) );
  rt.st(1);   
  return rt;
}


DEFINE_VERTEX( f_rS_D )
{
  Tensor::Index a,b,c,d;
//  Tensor X = LeviCivita()( -mu, -nu, -alpha, -beta ) * P(nu) * Orbital_DWave(P,Q)(alpha,c) * V1(-c,b) ;
  Tensor F = Spin1hProjector(P)(a,b) 
    * Gamma[4](b,d) 
    * gamma_twiddle(P)(mu,d,c)
    * V1(alpha,c) 
    * Orbital_DWave(P,Q)(-mu,-alpha) 
    * V2[0] / GeV;
  F.st();
  return F;
}

DEFINE_VERTEX( f_rS_P )
{
  Tensor::Index a,b,c;
  auto L = Orbital_PWave(P,Q);
  Tensor F = Spin1hProjector(P)(a,b)                 * V1(-mu,b) * L(mu) * V2[0] / GeV;
  F.st();
  return F;
}

DEFINE_VERTEX( f_rS_P1 )
{
  Tensor::Index a,b,c;
  auto L = Orbital_PWave(P,Q);
  Tensor F = Spin1hProjector(P)(a,b) * Gamma[4](b,c) * V1(-mu,c) * L(mu) * V2[0]/ GeV;
  F.st();
  return F;
}

DEFINE_VERTEX( S_ff_S )
{
  Tensor::Index a; 
  return Bar( V2 )(a) * V1(a);
}

DEFINE_VERTEX( S_ff_S1 )
{
  Tensor::Index a,b; 
  return Bar( V2 )(a) * Gamma[4](a,b) * V1(b);
}


DEFINE_VERTEX( V_ff_P )
{
  Tensor::Index a,b ; 
  return Bar( V2 )(a) * Gamma4Vec()(mu,a,b) * V1(b);
}

DEFINE_VERTEX( V_ff_P1 )
{
  Tensor::Index a,b,c ; 
  return Bar( V2 )(a) * Gamma[4](a,b) * Gamma4Vec()(mu,b,c) * V1(c);
}
