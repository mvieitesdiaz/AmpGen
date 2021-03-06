#include "AmpGen/Minimiser.h"

#include <TMatrixTBase.h>
#include <TMatrixTSym.h>
#include <TGraph.h>
#include <iostream>
#include <string>
#include <iomanip>
#include <Minuit2/Minuit2Minimizer.h>
#include <Minuit2/MinimumState.h>
#include <Minuit2/MnPrint.h>

#include "AmpGen/IExtendLikelihood.h"
#include "AmpGen/MinuitParameter.h"
#include "AmpGen/MinuitParameterSet.h"
#include "AmpGen/MsgService.h"
#include "AmpGen/NamedParameter.h"
#include "AmpGen/Utilities.h"
#include "Math/Factory.h"
#include "Math/Functor.h"
#include "Math/Minimizer.h"

using namespace AmpGen;
using namespace ROOT;

unsigned int Minimiser::nPars() const { return m_nParams; }

void Minimiser::operator()(int i, const ROOT::Minuit2::MinimumState & state) 
{
  std::cout<<"\n"<<std::endl;
  // Prints the FCN, Edm and Ncalls in a single line:
  ROOT::Minuit2::MnPrint::PrintState(std::cout, state, "iteration  #  ",i); 
  
  // Ensure sync with current parameter value inside Minuit (in particular: reproduces Minuit behaviour printing the same FCN and param values for the last and previous to last iteration --only the Edm value gets updated after running Hesse)
  for(size_t j = 0; j < m_mapping.size(); ++j ) {
    m_parSet->at( m_mapping[j] )->setCurrentFitVal( state.Vec()[j] );
  }
  
  // Retrieve the blinding offset 
  double blindingoffset = 0.;
  for(size_t ii = 0 ; ii < m_parSet->size(); ++ii) {
      auto par = m_parSet->at(ii);
      if ( par->isBlindingOffset() ) {
	blindingoffset = par->mean();
	break;
      }
  }

  // Print parameter values during the minimisation (blind params are shifted by the blinding offset)
  for(size_t iii = 0 ; iii < m_parSet->size(); ++iii) {
    auto par = m_parSet->at(iii);
    if ( !par->isFree() && !par->isBlind()  ) continue;      
    if (par->isBlind()){
      if (!blindingoffset){
	INFO("Attempting to print a blind parameter without blindingoffset. Check Minimiser.cpp");
	continue;
      }	
      std::cout<< par->name()<< " " << par->mean() + blindingoffset << "(BLIND)"<<std::endl;
    }
    else{
      std::cout<< par->name()<< " " << par->mean() <<std::endl;
    }
  }

  if (state.HasCovariance() )
    std::cout << "Error matrix change = " << state.Error().Dcovar() << std::endl;

}


double Minimiser::operator()( const double* xx )
{
  for(size_t i = 0; i < m_mapping.size(); ++i ) {
    m_parSet->at( m_mapping[i] )->setCurrentFitVal( xx[i] );
  }
  double LL = m_theFunction() ;
  for ( auto& extendTerm : m_extendedTerms ) {
    LL -= 2 * extendTerm->getVal();
  }
  return LL - m_ll_zero;
}

double Minimiser::FCN() const { return m_theFunction(); }
double Minimiser::Edm() const { return m_minimiser->Edm(); }
double Minimiser::NCalls() const {return m_minimiser->NCalls(); }

void Minimiser::gradientTest()
{
  for (size_t i = 0; i < m_mapping.size(); ++i) {
    auto parameter = m_parSet->at( m_mapping[i] );
    double m       = parameter->mean();
    parameter->setCurrentFitVal( parameter->meanInit() + parameter->stepInit() );
    double vp = FCN();
    parameter->setCurrentFitVal( parameter->meanInit() - parameter->stepInit() );
    double vm = FCN();
    if ( parameter->stepInit() == 0 ) {
      WARNING( "Step of free parameter: " << parameter->name() << " = 0" );
    } else {
      INFO( " dF/d{" << parameter->name() << "} = " << ( vp - vm ) / ( 2 * parameter->stepInit() ) );
    }
    parameter->setCurrentFitVal( m );
  }
}

void Minimiser::prepare()
{
  std::string algorithm = NamedParameter<std::string>( "Minimiser::Algorithm", "Hesse");
  size_t maxCalls       = NamedParameter<size_t>( "Minimiser::MaxCalls"  , 100000);
  double tolerance      = NamedParameter<double>( "Minimiser::Tolerance" , 1);
  m_printLevel          = NamedParameter<size_t>( "Minimiser::PrintLevel", 0); //MUST be 0 for blind fits
  m_normalise           = NamedParameter<bool>(   "Minimiser::Normalise",false);
  if ( m_minimiser != nullptr ) delete m_minimiser;
  m_minimiser = new Minuit2::Minuit2Minimizer(algorithm.c_str() );
  DEBUG( "Error definition = " << m_minimiser->ErrorDef() );
  m_minimiser->SetMaxFunctionCalls( maxCalls );
  m_minimiser->SetMaxIterations( 100000 );
  m_minimiser->SetTolerance( tolerance );
  m_minimiser->SetPrintLevel( m_printLevel );
  m_mapping.clear();
  m_covMatrix.clear();
  
   // Redundant check
  if (m_printLevel){
    for (size_t i = 0 ; i< m_parSet->size(); ++i){
      auto par = m_parSet->at(i);
      if (par->iFixInit() == MinuitParameter::Flag::Blind || par->iFixInit() == MinuitParameter::Flag::BlindingOffset){
	FATAL("Minimiser::PrintLevel is !=0 and there are parameters declared either as Blind or BlindingOffset, neither of which should have its real value printed. Set PrintLevel == 0 (in Minimiser.cpp) for a blind fit.");
      }
    }
  }
  
  for(size_t i = 0 ; i < m_parSet->size(); ++i) 
  {
    auto par = m_parSet->at(i);
    if ( ! par->isFree() && ! par->isBlind() ) continue;
    m_minimiser->SetVariable(m_mapping.size(), par->name(), par->mean(), par->stepInit());
    if ( par->minInit() != 0 || par->maxInit() != 0 ) 
      m_minimiser->SetVariableLimits( m_mapping.size(), par->minInit(), par->maxInit() );
    m_mapping.push_back(i);
    if ( m_printLevel != 0 ) INFO( *par );
  }
  m_nParams = m_mapping.size();
  m_covMatrix.resize( m_nParams * m_nParams, 0 );
}

bool Minimiser::doFit()
{
  if( m_normalise ) m_ll_zero = m_theFunction();
  ROOT::Math::Functor f( *this, m_nParams );
  for (size_t i = 0; i < m_mapping.size(); ++i ) {
    MinuitParameter* par = m_parSet->at( m_mapping[i] );
    m_minimiser->SetVariable( i, par->name(), par->mean(), par->stepInit() );
    if ( par->minInit() != 0 || par->maxInit() != 0 )
      m_minimiser->SetVariableLimits( i, par->minInit(), par->maxInit() );
  }
  m_minimiser->SetFunction( f );

  dynamic_cast< Minuit2::Minuit2Minimizer* >(m_minimiser)->SetTraceObject( *this );

  m_minimiser->Minimize();
  for (size_t i = 0; i < m_nParams; ++i ) {
    auto par = m_parSet->at( m_mapping[i] );
    double error = *( m_minimiser->Errors() + i );
    par->setResult( *( m_minimiser->X() + i ), error, error, error );
    for ( unsigned int j = 0; j < m_nParams; ++j ) {
      m_covMatrix[i + m_nParams * j] = m_minimiser->CovMatrix( i, j );
    }
  } 
  m_status = m_minimiser->Status();
  /*
  for( unsigned i = 0 ; i != m_nParams; ++i ){
    double low  = 0;
    double high = 0; 
    int status  = 0; 
    m_minimiser->GetMinosError(i, low, high, status); 
    auto param = m_parSet->at( m_mapping[i] ); 
    param->setResult( *param, param->err(), low, high  );
  }
  for( unsigned i = 0 ; i != m_nParams; ++i )
  {
    auto param = m_parSet->at( m_mapping[i] ); 
    INFO( param->name() << " " << param->mean() << " " << param->errPos() << " " << param->errNeg() );
  }
  */
  return 1;
}


TGraph* Minimiser::scan( MinuitParameter* param, const double& min, const double& max, const double& step )
{
 if ( param->isBlindingOffset() ) FATAL("Cannot perform a FCN scan for the blinding offset");

  double blindingoffset = 0.;
  if ( param->isBlind() ){ 
    for( size_t i = 0 ; i < m_parSet->size(); ++i){
	auto par = m_parSet->at(i);
	if ( par->isBlindingOffset() ){
	  blindingoffset = par->mean();
	  break;
	}
    }
  }

  param->fix();
  TGraph* rt = new TGraph();
  for ( double sv = min; sv < max; sv += step ) {
    param->setCurrentFitVal( sv - blindingoffset);
    doFit();
    rt->SetPoint( rt->GetN(), sv, FCN() );
  }
  return rt;
}

int Minimiser::status() const { return m_status; }

TMatrixTSym<double> Minimiser::covMatrix() const
{
  unsigned int internalPars = nPars();
  TMatrixTSym<double> matrix( internalPars );
  for ( unsigned int i = 0; i < internalPars; i++ ) {
    for ( unsigned int j = 0; j < internalPars; j++ ) {
      matrix( i, j ) = m_covMatrix[i + j * internalPars];
    }
  }
  return matrix;
}

TMatrixTSym<double> Minimiser::covMatrixFull() const
{
  unsigned int internalPars = m_parSet->size();
  TMatrixTSym<double> matrix( internalPars );
  for ( unsigned int i = 0; i < m_mapping.size(); ++i ) {
    for ( unsigned int j = 0; j < m_mapping.size(); j++ ) {
      matrix( m_mapping[i], m_mapping[j] ) = m_covMatrix[i + j * m_mapping.size()];
    }
  }
  return matrix;
}

MinuitParameterSet* Minimiser::parSet() const { return m_parSet; }

void Minimiser::addExtendedTerm( IExtendLikelihood* m_term )
{ 
  m_extendedTerms.push_back( m_term ); 
}

ROOT::Minuit2::Minuit2Minimizer* Minimiser::minimiserInternal() { return m_minimiser; }

