#ifndef Framework_Factory_h
#define Framework_Factory_h

#include "PluginManager/PluginFactory.h"
#include "FWCore/ParameterSet/interface/ParameterSet.h"
#include "FWCore/Framework/src/Worker.h"
#include "FWCore/Framework/src/WorkerMaker.h"
#include "FWCore/Framework/src/WorkerParams.h"

#include <map>
#include <string>
#include <memory>

namespace edm {

  class Factory : public seal::PluginFactory<Maker* ()>
  {
  public:
    typedef std::map<std::string, Maker*> MakerMap;

    ~Factory();

    static Factory* get();

    std::auto_ptr<Worker> makeWorker(const WorkerParams&) const;


  private:
    Factory();
    static Factory singleInstance_;
    mutable MakerMap makers_;
  };

}
#endif
