#include "DataFormats/Common/interface/Handle.h"
#include "DataFormats/Common/interface/Ptr.h"
#include "CommonTools/CandUtils/interface/AddFourMomenta.h"
#include "DataFormats/Candidate/interface/CompositeCandidate.h"
#include "../interface/PhotonIdAnalyzer.h"
#include "DataFormats/PatCandidates/interface/PackedGenParticle.h"
#include "FWCore/Utilities/interface/TypeWithDict.h"

#include "DataFormats/EcalRecHit/interface/EcalRecHitCollections.h"

#include <map>


using namespace std;
using namespace edm;
using namespace flashgg;
using namespace diphotons;

using pat::PackedGenParticle;
using reco::Candidate;
using reco::Vertex;

typedef enum { kFake, kPrompt  } genMatch_t;

struct GenMatchInfo {
	GenMatchInfo() : matched(0), nPhotonsInCone(0),
			 relPt(0.), deltaR(999.), 
			 extraEnergy(0.), match(kFake) 
		{};
	const PackedGenParticle * matched;
	int nPhotonsInCone;
	float relPt, deltaR;
	float extraEnergy;
	genMatch_t match;
};

/// default constructor
PhotonIdAnalyzer::PhotonIdAnalyzer(const edm::ParameterSet& cfg, TFileDirectory& fs): 
  edm::BasicAnalyzer::BasicAnalyzer(cfg, fs),
  photons_(cfg.getParameter<edm::InputTag>("photons")),
  packedGen_(cfg.getParameter<edm::InputTag>("packedGenParticles")),
  vertexes_(cfg.getParameter<edm::InputTag> ("vertexes")),
  lumiWeight_(cfg.getParameter<double>("lumiWeight")),
  /// photonFunctor_(edm::TypeWithDict(edm::Wrapper<vector<Photon> >::typeInfo())),
  promptTree_(0), fakesTree_(0)
{
  hists_["photonPt"  ]	     = fs.make<TH1F>("photonPt"		, "pt"           ,  100,  0., 300.);
  hists_["photonEta" ]	     = fs.make<TH1F>("photonEta"	, "eta"          ,  100, -3.,   3.);
  hists_["photonPhi" ]	     = fs.make<TH1F>("photonPhi"	, "phi"          ,  100, -5.,   5.); 
  hists_["promptPhotonN"  ]  = fs.make<TH1F>("promptPhotonN"	, "n"            ,  6,  -0.5, 5.5);
  hists_["promptPhotonPt"  ] = fs.make<TH1F>("promptPhotonPt"	, "pt"           ,  100,  0., 300.);
  hists_["promptPhotonEta" ] = fs.make<TH1F>("promptPhotonEta"	, "eta"          ,  100, -3.,   3.);
  hists_["promptPhotonPhi" ] = fs.make<TH1F>("promptPhotonPhi"	, "phi"          ,  100, -5.,   5.); 
  hists_["fakePhotonN"  ]  = fs.make<TH1F>("fakePhotonN"	, "n"            ,  6,  -0.5, 5.5);
  hists_["fakePhotonPt"  ]   = fs.make<TH1F>("fakePhotonPt"	, "pt"           ,  100,  0., 300.);
  hists_["fakePhotonEta" ]   = fs.make<TH1F>("fakePhotonEta"	, "eta"          ,  100, -3.,   3.);
  hists_["fakePhotonPhi" ]   = fs.make<TH1F>("fakePhotonPhi"	, "phi"          ,  100, -5.,   5.); 
  /// hists_["genPairsMass" ]    = fs.make<TH1F>("genPairsMass"	, "genPairsMass" ,  500,  0,    1.);
  hists_["matchMinDr" ]    = fs.make<TH1F>("matchMinDr"	, "matchMinDr" ,  100,  0,    0.1);
  hists_["matchRelPtOnePhoton" ]    = fs.make<TH1F>("matchRelPtOnePhoton"	, "matchRelPtOnePhoton" ,  100,  0,    1.5);
  hists_["matchRelPtIso" ]    = fs.make<TH1F>("matchRelPtIso"	, "matchRelPtIso" ,  100,  0,    1.5);
  hists_["matchRelPtNonIso" ]    = fs.make<TH1F>("matchRelPtNonIso"	, "matchRelPtNonIso" ,  100,  0,    1.5);
  hists_["matchNPartInCone" ]   = fs.make<TH1F>("matchNPartInCone", "matchNPartInCone" , 21,  -0.5,   20);
  hists_["matchExtraEnergy" ]    = fs.make<TH1F>("matchExtraEnergy"	, "matchExtraEnergy" ,  100,  0,    1.5);


  
  vector<ParameterSet> miniTreeCfg = cfg.getUntrackedParameter<vector<ParameterSet> >("miniTreeCfg",vector<ParameterSet>());
  if( ! miniTreeCfg.empty() ) {
	  for( vector<ParameterSet>::iterator ivar=miniTreeCfg.begin(); ivar!=miniTreeCfg.end(); ++ivar ) {
		  string method = ivar->getUntrackedParameter<string>("var");
		  string name = ivar->getUntrackedParameter<string>("name","");
		  double def = ivar->getUntrackedParameter<double>("default",0.);
		  if( name.empty() ) { name = method; }
		  /// cout << "miniTreeCfg " << name << " " << method << endl;
		  miniTreeBranches_.push_back(name);
		  miniTreeFunctors_.push_back(PhotonFunctor(method));
		  //// miniTreeFunctors_.push_back();
		  /// photonFunctor_.addExpression(method);
		  miniTreeBuffers_.push_back(def);
	  }
	  miniTreeDefaults_ = miniTreeBuffers_;
	  promptTree_ = bookTree("promptTree",fs);
	  fakesTree_ = bookTree("fakesTree",fs);
  }
  
}


TTree * PhotonIdAnalyzer::bookTree(const string & name, TFileDirectory& fs)
{
	TTree * ret = fs.make<TTree>(name.c_str(),name.c_str());
	
	ret->Branch("ipho",&ipho_,"ipho/I");
	ret->Branch("weight",&weight_,"weight/F");
	for(size_t ibr=0; ibr<miniTreeBuffers_.size(); ++ibr) {
		/// cout << "miniTree branch "  << miniTreeBranches_[ibr] << endl;
		ret->Branch(Form("%s",miniTreeBranches_[ibr].c_str()),&miniTreeBuffers_[ibr],Form("%s/F",miniTreeBranches_[ibr].c_str()));
	}
	return ret;
}

void PhotonIdAnalyzer::fillTreeBranches(const Photon & pho)
{
	for(size_t ibr=0; ibr<miniTreeFunctors_.size(); ++ibr) {
		miniTreeBuffers_[ibr] = miniTreeFunctors_[ibr](pho);
	}
}


PhotonIdAnalyzer::~PhotonIdAnalyzer()
{
}

void PhotonIdAnalyzer::beginJob()
{

}

/// everything that needs to be done after the event loop
void PhotonIdAnalyzer::endJob()
{
}

float PhotonIdAnalyzer::getEventWeight(const edm::EventBase& event)
{
	return lumiWeight_;
}

// MC truth
GenMatchInfo doGenMatch(const Photon & pho, const vector<PackedGenParticle> & genParts, float maxDr, float minLeadPt, float minPtRelOnePhoton,
		      float minPtRel, float maxExtraEnergy// , float weight,
		      // TH1 * matchMinDr,  TH1 * matchRelPtOnePhoton,  TH1 * matchRelPtIso, TH1 * matchRelPtNonIso, TH1 * matchNPartInCone, TH1 * matchExtraEnergy)
	)
{
	/// genMatch_t ret = kFake;
	GenMatchInfo ret;
	// look for gen level photons in the reco photon direction
	map<float,const PackedGenParticle *> genPhotonsInCone; // use map to sort candidates in deltaR
	Candidate::LorentzVector cluster( 0, 0, 0, 0 );
	for(vector<PackedGenParticle>::const_iterator igen=genParts.begin(); igen!=genParts.end(); ++igen) {
		if( igen->pdgId() != 22 ) { continue; }
		float dR = reco::deltaR(pho,*igen);
		if(dR < maxDr) {
			genPhotonsInCone.insert(make_pair(dR,&(*igen)));
			cluster += igen->p4();
		}
	}
	ret.nPhotonsInCone = genPhotonsInCone.size();
	
	if(!genPhotonsInCone.empty()){
		// find the closeset gen photon above threshold
		for(map<float,const PackedGenParticle *>::iterator ipair=genPhotonsInCone.begin(); ipair!=genPhotonsInCone.end(); ++ipair ) {
			if( ipair->second->pt() > minLeadPt ) {
				ret.matched = ipair->second;
				break;
			}
		}
		if( ret.matched == 0 ) { 
			/// matchMinDr->Fill(maxDr,weight);
			return ret; 
		}
		if( ret.matched->numberOfMothers() > 0 ) {
			for(size_t imom=0; imom<ret.matched->numberOfMothers(); ++imom) {
				int mstat = ret.matched->mother(imom)->status();
				int mpdgId = ret.matched->mother(imom)->pdgId();
				if( mpdgId == 22 && (mstat == 3 || mstat == 23 || mstat == 22) ) {
					ret.match = kPrompt;
					/// matchRelPtOnePhoton->Fill(ret.matched->pt()/pho.pt(),weight);
				}
				
			}
		}
		ret.extraEnergy = (cluster.energy() - ret.matched->energy())/ret.matched->energy();
		ret.relPt = ret.matched->pt()/pho.pt();
		ret.deltaR = reco::deltaR(pho,*ret.matched);
		// if not ME photon, check harder
		if( ret.match != kPrompt ) {
			/// matchMinDr->Fill(ret.deltaR,weight);
			/// matchNPartInCone->Fill(genPhotonsInCone.size(),weight);
			if( ( genPhotonsInCone.size() == 1 ) ) {
				// if only one photon is found in the cone apply the corresponding pt cut
				/// matchRelPtOnePhoton->Fill(re.relPt,weight);
				if( ( ret.matched->pt() > minPtRelOnePhoton*pho.pt() ) ) {
					ret.match = kPrompt;
				}
			} else { 
				// if more then one photon is found in the cone, require the extra 
				//   photons contribution to be below some threshold
				/// matchExtraEnergy->Fill(ret.extraEnergy,weight);
				if( ret.extraEnergy < maxExtraEnergy ) {
					/// matchRelPtIso->Fill(ret.relPt,weight);
					if( ( ret.matched->pt() > minPtRel*pho.pt() ) ) { 
						ret.match = kPrompt; 
					}
				} else {
					/// matchRelPtNonIso->Fill(ret.relPt,weight);
				}
			}
		}
	}
	//// if( matched != 0 && matched->numberOfMothers() > 0 ) {
	//// 	for(size_t imom=0; imom<matched->numberOfMothers(); ++imom) {
	//// 		int mstat = matched->mother(imom)->status();
	//// 		int mpdgId = matched->mother(imom)->pdgId();
	//// 		cout << "match " << ret << " mstat " << mstat << " mpdgId " << mpdgId << endl;
	//// 	}
	//// }
	return ret;
}


/// everything that needs to be done during the event loop
void 
PhotonIdAnalyzer::analyze(const edm::EventBase& event)
{
  // Handle to the photon collection
  Handle<vector<Photon> > photons;
  Handle<vector<PackedGenParticle> > packedGenParticles;
  Handle<vector<Vertex> > vertexes;
  
  // Handle<vector<PrunedGenParticle> > prunedGenParticles;
  event.getByLabel(photons_, photons);
  // event.getByLabel(prunedGenParticles_, prunedGenParticles);
  event.getByLabel(packedGen_, packedGenParticles);
  event.getByLabel(vertexes_,vertexes);
  
  weight_ = getEventWeight(event);
  
  // loop photon collection and fill histograms
  std::vector<GenMatchInfo> genMatch;
  int nPrompt=false, nFakes = false;
  ipho_ = 0;
  for(std::vector<Photon>::const_iterator ipho=photons->begin(); ipho!=photons->end(); ++ipho){
	  
	  Photon * pho = ipho->clone();

	  float scEta = pho->superCluster()->eta();
	  if( fabs(scEta)>2.5 || (fabs(scEta)>1.4442 && fabs(scEta)<1.556 ) ) { continue; }

	  GenMatchInfo match = doGenMatch(*pho,*packedGenParticles,0.1,15.,0.,0.,0.05
					  /// ,weight_,
					  /// hists_["matchMinDr"],hists_["matchRelPtOnePhoton"],
					  /// hists_["matchRelPtIso"],hists_["matchRelPtNonIso"],
					  /// hists_["matchNPartInCone"],hists_["matchExtraEnergy"]
		  );
	  /// cout << "match " << match << endl;
	  genMatch.push_back( match );
	  if( match.matched )  {
		  pho->addUserFloat("etrue",match.matched->energy());
	  } else {
		  pho->addUserFloat("etrue",0.);
	  }
	  pho->addUserFloat("dRMatch",match.deltaR);
	  
	  DetId seedId = pho->superCluster()->seed()->seed();
	  cout << " rechits " << pho->recHits()->size() << endl ;
	                   
	  EcalRecHitCollection::const_iterator seedRh = pho->recHits()->find(seedId);
	  if( seedRh != pho->recHits()->end() ) {
		  pho->addUserInt("seedRecoFlag",seedRh->recoFlag());
	  } else {
		  pho->addUserInt("seedRecoFlag",-1);
	  }
	  
	  for(size_t iv=0; iv<vertexes->size(); ++iv) {
		  Ptr<Vertex> vtx(vertexes,iv);
		  pho->addUserFloat(Form("chgIsoWrtVtx%d",(int)iv), pho->getpfChgIso03WrtVtx(vtx));
	  }

	  fillTreeBranches(*pho);
	  
	  if( match.match == kPrompt ) {
		  if( nPrompt ==0 ) { 
			  hists_["promptPhotonPt" ]->Fill( pho->pt (), weight_ );
			  hists_["promptPhotonEta"]->Fill( pho->eta(), weight_ );
			  hists_["promptPhotonPhi"]->Fill( pho->phi(), weight_ );
			  if( promptTree_ ) {
				  promptTree_->Fill();
			  }

		  }
		  ++nPrompt;
	  } else {
		  if(  nFakes == 0 ) {
			  hists_["fakePhotonPt" ]->Fill( pho->pt (), weight_ );
			  hists_["fakePhotonEta"]->Fill( pho->eta(), weight_ );
			  hists_["fakePhotonPhi"]->Fill( pho->phi(), weight_ );
		  }
		  if( fakesTree_ ) {
			  fakesTree_->Fill();
		  }
		  ++nFakes;
	  }
	  
	  hists_["photonPt" ]->Fill( pho->pt (), weight_ );
	  hists_["photonEta"]->Fill( pho->eta(), weight_ );
	  hists_["photonPhi"]->Fill( pho->phi(), weight_ );

	  ++ipho_;
	  delete pho;
  }

  hists_["promptPhotonN" ]->Fill(nPrompt, weight_);
  hists_["fakePhotonN" ]->Fill(nFakes, weight_);
  
  
}

