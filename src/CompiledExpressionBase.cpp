#include "AmpGen/CompiledExpressionBase.h"

#include <algorithm>
#include <memory>
#include <ostream>

#include "AmpGen/CacheTransfer.h"
#include "AmpGen/MinuitParameterSet.h"
#include "AmpGen/MsgService.h"
#include "AmpGen/NamedParameter.h"
#include "AmpGen/Utilities.h"
#include "AmpGen/ThreadPool.h"
#include "AmpGen/CompilerWrapper.h"
#include "AmpGen/ASTResolver.h"

using namespace AmpGen;

std::string AmpGen::programatic_name( std::string s )
{
  std::replace( s.begin(), s.end(), '-', 'm' );
  std::replace( s.begin(), s.end(), '+', 'p' );
  std::replace( s.begin(), s.end(), '-', 'm' );
  std::replace( s.begin(), s.end(), ',', '_' );
  std::replace( s.begin(), s.end(), '(', '_' );
  std::replace( s.begin(), s.end(), ')', '_' );
  std::replace( s.begin(), s.end(), '[', '_' );
  std::replace( s.begin(), s.end(), ']', '_' );
  std::replace( s.begin(), s.end(), '{', '_' );
  std::replace( s.begin(), s.end(), '}', '_' );
  std::replace( s.begin(), s.end(), '*', 's' );
  std::replace( s.begin(), s.end(), ':', '_' );
  std::replace( s.begin(), s.end(), '\'', '_' );
  return s;
}

void CompiledExpressionBase::resolve(const MinuitParameterSet* mps)
{
  if( m_resolver != nullptr ) delete m_resolver ; 
  m_resolver = new ASTResolver( m_evtMap, mps );
  m_resolver->getOrderedSubExpressions( m_obj,  m_dependentSubexpressions );
  for ( auto& sym : m_db ) sym.second.resolve( *m_resolver ); 
    //    resolver.getOrderedSubExpressions( sym.second, m_dependentSubexpressions  );
  resolveParameters(*m_resolver);
}

CompiledExpressionBase::CompiledExpressionBase( const Expression& expression, 
    const std::string& name, 
    const DebugSymbols& db,
    const std::map<std::string, size_t>& evtMapping )
  : m_obj( expression ), 
  m_name( name ),
  m_db(db),
  m_evtMap(evtMapping),
  m_readyFlag(nullptr) {}

CompiledExpressionBase::CompiledExpressionBase( const std::string& name ) 
  : m_name( name ),
  m_readyFlag(nullptr) {}

std::string CompiledExpressionBase::name() const { return m_name; }
unsigned int CompiledExpressionBase::hash() const { return FNV1a_hash(m_name); }

void CompiledExpressionBase::resolveParameters( ASTResolver& resolver )
{
  auto ppdf = this;
  m_cacheTransfers.clear();
  for( auto& expression : resolver.cacheFunctions() ) 
    m_cacheTransfers.emplace_back( expression.second ); 
  ppdf->resizeExternalCache(resolver.nParams() ); 
  prepare();
}

void CompiledExpressionBase::prepare()
{
  for ( auto& t : m_cacheTransfers ) t->transfer( this );
}

void CompiledExpressionBase::addDependentExpressions( std::ostream& stream, size_t& sizeOfStream ) const
{
  for ( auto& dep : m_dependentSubexpressions ) {
    std::string rt = "auto v" + std::to_string(dep.first) + " = " + dep.second.to_string(m_resolver) +";"; 
    stream << rt << "\n";
    sizeOfStream += sizeof(char) * rt.size(); /// bytes /// 
  }
}

void CompiledExpressionBase::to_stream( std::ostream& stream  ) const 
{
  if( m_db.size() !=0 ) stream << "#include<iostream>\n"; 
  stream << "extern \"C\" const char* " << m_name << "_name() {  return \"" << m_name << "\"; } \n";
  bool enable_cuda = NamedParameter<bool>("enable_cuda",false);

    size_t sizeOfStream = 0; 
  if( !enable_cuda ){
    // Avoid a warning about std::complex not being C compatible (it is)
    stream << "#pragma clang diagnostic push\n"
      << "#pragma clang diagnostic ignored \"-Wreturn-type-c-linkage\"\n";

    stream << "extern \"C\" " << returnTypename() << " " << m_name << "(" << fcnSignature() << "){\n";
    addDependentExpressions( stream , sizeOfStream );
    std::string objString = m_obj.to_string(m_resolver);
    stream << "return " << objString << ";\n}\n";
  }
  else {
    stream << "__global__ void " << m_name << "( float* r, const float* x0, const float3* x1 , const int& N) { \n";
    stream <<  "  int i     = blockIdx.x * blockDim.x + threadIdx.x;\n";
    addDependentExpressions( stream, sizeOfStream);
    std::string objString = m_obj.to_string(m_resolver);
    stream << "  r[i] = " << objString << ";\n}\n";
  }

  if( NamedParameter<bool>("CompiledExpressionBase::Compat") == true ){
    stream << "#pragma clang diagnostic pop\n\n";
    stream << "extern \"C\" void " <<  m_name << "_c" << "(double *real, double *imag, " << fcnSignature() << "){\n";
    stream << "  auto val = " << m_name << "(" << args() << ") ;\n"; 
    stream << "  *real = val.real();\n";
    stream << "  *imag = val.imag();\n";
    stream << "}\n";
  }
  if ( m_db.size() != 0 ) addDebug( stream );
}

std::ostream& AmpGen::operator<<( std::ostream& os, const CompiledExpressionBase& expression )
{
  expression.to_stream(os);
  return os; 
}

void CompiledExpressionBase::compile(const std::string& fname, const bool& wait  )
{
  if( ! wait ){
  m_readyFlag = new std::shared_future<bool>( ThreadPool::schedule([this,fname](){
        CompilerWrapper().compile(*this,fname);
        return true;} ) );
  }
  else {
    CompilerWrapper(false).compile(*this,fname );
  }
}

void CompiledExpressionBase::addDebug( std::ostream& stream ) const
{
  stream << "extern \"C\" std::vector<std::complex<double>> " << m_name << "_DB(" << fcnSignature() << "){\n";
  size_t sizeOf = 0 ; 
  addDependentExpressions( stream, sizeOf );
  stream << "return {";
  for ( unsigned int i = 0 ; i < m_db.size(); ++i ) {
    std::string comma = (i!=m_db.size()-1)?", " :"};\n}\n";
    const auto expression = m_db[i].second; 
    if ( expression.to_string(m_resolver) != "NULL" )
    stream << std::endl << expression << comma;
    else stream << std::endl << "-999" << comma ;
  }
}