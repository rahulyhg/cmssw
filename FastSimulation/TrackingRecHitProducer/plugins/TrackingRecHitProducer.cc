#include "TrackingRecHitProducer.h"

#include "Geometry/Records/interface/TrackerDigiGeometryRecord.h"
#include "Geometry/Records/interface/TrackerTopologyRcd.h"

#include "FastSimulation/TrackingRecHitProducer/interface/TrackerDetIdSelector.h"

#include "DataFormats/TrackerRecHit2D/interface/FastSingleTrackerRecHit.h"
#include "DataFormats/TrackerRecHit2D/interface/FastTrackerRecHitCollection.h"

#include "DataFormats/Common/interface/RefVector.h"

#include <map>
#include <memory>

TrackingRecHitProducer::TrackingRecHitProducer(const edm::ParameterSet& config)
{
    edm::ConsumesCollector consumeCollector = consumesCollector();
    const std::vector<edm::ParameterSet>& pluginConfigs = config.getParameter<std::vector<edm::ParameterSet>>("plugins");

    for (unsigned int iplugin = 0; iplugin<pluginConfigs.size(); ++iplugin)
    {
        const edm::ParameterSet& pluginConfig = pluginConfigs[iplugin];
        const std::string pluginType = pluginConfig.getParameter<std::string>("type");
        const std::string pluginName = pluginConfig.getParameter<std::string>("name");

        TrackingRecHitAlgorithm* recHitAlgorithm = TrackingRecHitAlgorithmFactory::get()->tryToCreate(pluginType,pluginName,pluginConfig,consumeCollector);
        if (recHitAlgorithm)
        {
            std::cout<<"TrackingRecHitProducer: adding plugin '"<<pluginName<<"' as '"<<recHitAlgorithm->getName()<<"'"<<std::endl;
            _recHitAlgorithms.push_back(recHitAlgorithm);
        }
        else
        {
            edm::LogWarning("TrackingRecHitAlgorithm plugin not found: ") << "plugin name = "<<pluginName<<"\nconfiguration=\n"<<pluginConfig.dump();
        }
    }

    edm::InputTag simHitTag = config.getParameter<edm::InputTag>("simHits");
    _simHitToken = consumes<std::vector<PSimHit>>(simHitTag);

    produces<FastTrackerRecHitCollection>();
    produces<FastTrackerRecHitRefCollection>("simHit2RecHitMap");
}

void TrackingRecHitProducer::beginStream(edm::StreamID id)
{
    for (TrackingRecHitAlgorithm* algo: _recHitAlgorithms)
    {
        algo->beginStream(id);
    }
}

void TrackingRecHitProducer::beginRun(edm::Run const&, const edm::EventSetup& eventSetup)
{

}

void TrackingRecHitProducer::setupDetIdPipes(const edm::EventSetup& eventSetup)
{
    if (_iovSyncValue!=eventSetup.iovSyncValue())
    {
        _iovSyncValue=eventSetup.iovSyncValue();
        edm::ESHandle<TrackerGeometry> trackerGeometryHandle;
        edm::ESHandle<TrackerTopology> trackerTopologyHandle;
        eventSetup.get<TrackerDigiGeometryRecord>().get(trackerGeometryHandle);
        eventSetup.get<TrackerTopologyRcd>().get(trackerTopologyHandle);
        const TrackerGeometry* trackerGeometry = trackerGeometryHandle.product();
        const TrackerTopology* trackerTopology = trackerTopologyHandle.product();

        _detIdPipes.clear();

        std::cout<<"TrackingRecHitProducer: setting up pipes"<<std::endl;
        //build pipes for all detIds
        const std::vector<DetId>& detIds = trackerGeometry->detIds();
        std::vector<unsigned int> numberOfDetIdsPerAlgorithm(_recHitAlgorithms.size(),0);

        for (const DetId& detId: detIds)
        {
            TrackerDetIdSelector selector(detId,*trackerTopology);

            TrackingRecHitPipe pipe;
            for (unsigned int ialgo = 0; ialgo < _recHitAlgorithms.size(); ++ialgo)
            {
                TrackingRecHitAlgorithm* algo = _recHitAlgorithms[ialgo];
                if (selector.passSelection(algo->getSelectionString()))
                {
                    numberOfDetIdsPerAlgorithm[ialgo]+=1;
                    pipe.addAlgorithm(algo);
                }
            }
            _detIdPipes[detId.rawId()]=pipe;
        }
        for (unsigned int ialgo = 0; ialgo < _recHitAlgorithms.size(); ++ialgo)
        {
            std::cout<<"TrackingRecHitProducer: "<<_recHitAlgorithms[ialgo]->getName()<<": "<<numberOfDetIdsPerAlgorithm[ialgo]<<" dets"<<std::endl;
        }
    }
}

void TrackingRecHitProducer::produce(edm::Event& event, const edm::EventSetup& eventSetup)
{


    //resetup pipes if new iov
    setupDetIdPipes(eventSetup);
    //begin event
    for (TrackingRecHitAlgorithm* algo: _recHitAlgorithms)
    {
        algo->beginEvent(event,eventSetup);
    }

    //build DetId -> PSimHit map
    edm::Handle<std::vector<PSimHit>> simHits;
    event.getByToken(_simHitToken,simHits);

    // note from lukas:
    // a simple vector to store all tracker rechits
    std::unique_ptr<FastTrackerRecHitCollection> output_recHits(new FastTrackerRecHitCollection);
    output_recHits->reserve(simHits->size());
    
    // note from lukas
    // a vector or references to tracker rechits
    // one entry per simhit
    // each enty is to contain the reference to the rechit to the associated simhit
    // (=> several simhits can point to the same rechit)
    edm::RefProd<FastTrackerRecHitCollection> output_recHits_refProd = e.getRefBeforePut<FastTrackerRecHitCollection>();
    std::unique_ptr<FastTrackerRecHitRefCollection> output_recHitRefs(new FastTrackerRecHitRefCollection(simHits->size(),FastTrackerRecHitRef()));

    // note from lukas
    // output_recHitRefs is partially initialised
    // to make simHit i point to rechit j do
    // (*output_recHitRefs)[j] = FastTrackerRecHitRef(output_recHits_refProd,output_recHits->size()-1)
    // if you want a SimHit not to point to any RecHit, just leave the respective entry as it is, a null reference

    std::cout<<"TrackingRecHitProducer: N(SimHits input)="<<simHits->size()<<std::endl;
    
    std::map<unsigned int,std::vector<const PSimHit*>> hitsPerDetId;
    for (unsigned int ihit = 0; ihit < simHits->size(); ++ihit)
    {
        const PSimHit* simHit = &(*simHits)[ihit];
        hitsPerDetId[simHit->detUnitId()].push_back(simHit);
    }

    
    unsigned int nRecHits = 0;
    
    for (std::map<unsigned int,std::vector<const PSimHit*>>::iterator simHitsIt = hitsPerDetId.begin(); simHitsIt != hitsPerDetId.end(); ++simHitsIt)
    {
        const DetId& detId = simHitsIt->first;
        std::map<unsigned int, TrackingRecHitPipe>::const_iterator pipeIt = _detIdPipes.find(detId);
        if (pipeIt!=_detIdPipes.cend())
        {
            std::vector<const PSimHit*>& simHits = simHitsIt->second;
            const TrackingRecHitPipe& pipe = pipeIt->second;

            TrackingRecHitProductPtr product = std::make_shared<TrackingRecHitProduct>(detId,simHits);

            product = pipe.produce(product);
            nRecHits+=product->numberOfRecHits();
        }
        else
        {
            //there should be at least an empty pipe for each DetId
            throw cms::Exception("FastSimulation/TrackingRecHitProducer","A PSimHit carries a DetId which does not belong to the TrackerGeometry: "+std::to_string(simHitsIt->first));
        }
    }
    std::cout<<"TrackingRecHitProducer: N(RecHits produced)="<<nRecHits<<std::endl;
    
    //end event
    for (TrackingRecHitAlgorithm* algo: _recHitAlgorithms)
    {
        algo->endEvent(event,eventSetup);
    }

    event.put(std::move(output_recHits));
    event.put(std::move(output_recHitRefs),"simHit2RecHitMap");

}

void TrackingRecHitProducer::endStream()
{
    for (TrackingRecHitAlgorithm* algo: _recHitAlgorithms)
    {
        algo->endStream();
    }
}

TrackingRecHitProducer::~TrackingRecHitProducer()
{
}


DEFINE_FWK_MODULE(TrackingRecHitProducer);
