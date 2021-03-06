#include <iostream>

#include "FWCore/FWLite/interface/FWLiteEnabler.h"
#include "FWCore/PythonParameterSet/interface/MakeParameterSets.h"
#include "FWCore/ParameterSet/interface/ParameterSet.h"

#include "UserCode/bsmhiggs_fwk/interface/PatUtils.h"
#include "UserCode/bsmhiggs_fwk/interface/MacroUtils.h"
#include "UserCode/bsmhiggs_fwk/interface/DataEvtSummaryHandler.h"
#include "UserCode/bsmhiggs_fwk/interface/BSMPhysicsEvent.h"
#include "UserCode/bsmhiggs_fwk/interface/SmartSelectionMonitor.h"
#include "UserCode/bsmhiggs_fwk/interface/PDFInfo.h"
#include "UserCode/bsmhiggs_fwk/interface/rochcor2016.h" 
#include "UserCode/bsmhiggs_fwk/interface/muresolution_run2.h" 
#include "UserCode/bsmhiggs_fwk/interface/LeptonEfficiencySF.h"
#include "UserCode/bsmhiggs_fwk/interface/BTagCalibrationStandalone.h"
#include "UserCode/bsmhiggs_fwk/interface/BtagUncertaintyComputer.h"
//#include "UserCode/bsmhiggs_fwk/interface/METUtils.h"
//#include "UserCode/bsmhiggs_fwk/interface/BTagUtils.h"
//#include "UserCode/bsmhiggs_fwk/interface/EventCategory.h"

#include "CondFormats/JetMETObjects/interface/JetResolution.h"
#include "CondFormats/JetMETObjects/interface/JetCorrectionUncertainty.h"
#include "PhysicsTools/Utilities/interface/LumiReWeighting.h"

#include "TSystem.h"
#include "TFile.h"
#include "TTree.h"
#include "TCanvas.h"
#include "TH1F.h"
#include "TH2F.h"
#include "TProfile.h"
#include "TEventList.h"
#include "TROOT.h"
#include "TMath.h"


using namespace std;


namespace LHAPDF {
void initPDFSet(int nset, const std::string& filename, int member=0);
int numberPDF(int nset);
void usePDFMember(int nset, int member);
double xfx(int nset, double x, double Q, int fl);
double getXmin(int nset, int member);
double getXmax(int nset, int member);
double getQ2min(int nset, int member);
double getQ2max(int nset, int member);
void extrapolate(bool extrapolate=true);
}

struct stPDFval {
    stPDFval() {}
    stPDFval(const stPDFval& arg) :
        qscale(arg.qscale),
        x1(arg.x1),
        x2(arg.x2),
        id1(arg.id1),
        id2(arg.id2) {
    }

    double qscale;
    double x1;
    double x2;
    int id1;
    int id2;
};

//https://twiki.cern.ch/twiki/bin/viewauth/CMS/BtagRecommendation76X
const float CSVLooseWP = 0.5426;  // Updated to 80X Moriond17 Loose
const float CSVMediumWP = 0.800;
const float CSVTightWP = 0.935;

//https://twiki.cern.ch/twiki/bin/viewauth/CMS/Hbbtagging#8_0_X
// https://indico.cern.ch/event/543002/contributions/2205058/attachments/1295600/1932387/cv-doubleb-tagging-btv-approval.pdf (definition of WPs: slide 16)
const float DBLooseWP = 0.3;
const float DBMediumWP = 0.6;
const float DBTightWP = 0.9;

int main(int argc, char* argv[])
{
    //##################################################################################
    //##########################    GLOBAL INITIALIZATION     ##########################
    //##################################################################################

    // check arguments
    if(argc<2) {
        std::cout << "Usage : " << argv[0] << " parameters_cfg.py" << std::endl;
        exit(0);
    }

    // load framework libraries
    gSystem->Load( "libFWCoreFWLite" );
    //AutoLibraryLoader::enable();
    FWLiteEnabler::enable();

    // configure the process
    const edm::ParameterSet &runProcess = edm::readPSetsFrom(argv[1])->getParameter<edm::ParameterSet>("runProcess");

    bool isMC       = runProcess.getParameter<bool>("isMC");
    double xsec = runProcess.getParameter<double>("xsec");

    int mctruthmode = runProcess.getParameter<int>("mctruthmode");
    bool usemetNoHF = runProcess.getParameter<bool>("usemetNoHF");

    TString dtag=runProcess.getParameter<std::string>("tag");
    TString suffix=runProcess.getParameter<std::string>("suffix");

    bool verbose = runProcess.getParameter<bool>("verbose");

    TString url=runProcess.getParameter<std::string>("input");
    TString outFileUrl(gSystem->BaseName(url));
    outFileUrl.ReplaceAll(".root","");
    if(mctruthmode!=0) {
        outFileUrl += "_filt";
        outFileUrl += mctruthmode;
    }
    TString outdir=runProcess.getParameter<std::string>("outdir");
    TString outUrl( outdir );
    gSystem->Exec("mkdir -p " + outUrl);


    TString outTxtUrl_final= outUrl + "/" + outFileUrl + "_FinalList.txt";
    FILE* outTxtFile_final = NULL;
    outTxtFile_final = fopen(outTxtUrl_final.Data(), "w");
    printf("TextFile URL = %s\n",outTxtUrl_final.Data());
    fprintf(outTxtFile_final,"run lumi event\n");

    int fType(0);
    if(url.Contains("DoubleEG")) fType=EE;
    if(url.Contains("DoubleMuon"))  fType=MUMU;
    if(url.Contains("MuonEG"))      fType=EMU;
    if(url.Contains("SingleMuon"))  fType=MUMU;
    if(url.Contains("SingleElectron")) fType=EE;
    bool isSingleMuPD(!isMC && url.Contains("SingleMuon"));
    bool isDoubleMuPD(!isMC && url.Contains("DoubleMuon"));
    bool isSingleElePD(!isMC && url.Contains("SingleElectron"));
    bool isDoubleElePD(!isMC && url.Contains("DoubleEG"));

    bool isMC_ZZ2L2Nu  = isMC && ( string(url.Data()).find("TeV_ZZTo2L2Nu")  != string::npos);
    bool isMC_ZZTo4L   = isMC && ( string(url.Data()).find("TeV_ZZTo4L")  != string::npos);
    bool isMC_ZZTo2L2Q = isMC && ( string(url.Data()).find("TeV_ZZTo2L2Q")  != string::npos);

    bool isMC_WZ  = isMC && ( string(url.Data()).find("TeV_WZamcatnloFXFX")  != string::npos
                              || string(url.Data()).find("MC13TeV_WZpowheg")  != string::npos );

    bool isMC_VVV = isMC && ( string(url.Data()).find("MC13TeV_WZZ")  != string::npos
                              || string(url.Data()).find("MC13TeV_WWZ")  != string::npos
                              || string(url.Data()).find("MC13TeV_ZZZ")  != string::npos );

    bool isMCBkg_runPDFQCDscale = (isMC_ZZ2L2Nu || isMC_ZZTo4L || isMC_ZZTo2L2Q ||
                                   isMC_WZ || isMC_VVV);

    bool isMC_ttbar = isMC && (string(url.Data()).find("TeV_TT")  != string::npos);
    bool isMC_stop  = isMC && (string(url.Data()).find("TeV_SingleT")  != string::npos);

    bool isMC_Wh = isMC && (string(url.Data()).find("Wh")  != string::npos); 
    bool isMC_Zh = isMC && (string(url.Data()).find("Zh")  != string::npos); 
    bool isMC_VBF = isMC && (string(url.Data()).find("VBF")  != string::npos); 

    bool isSignal = (isMC_Wh || isMC_Zh || isMC_VBF );
    if (isSignal) printf("Signal url = %s\n",url.Data());

    //b-tagging: beff and leff must be derived from the MC sample using the discriminator vs flavor
    //the scale factors are taken as average numbers from the pT dependent curves see:
    //https://twiki.cern.ch/twiki/bin/viewauth/CMS/BtagPOG#2012_Data_and_MC_EPS13_prescript
    BTagSFUtil btsfutil;
    float beff(0.68), sfb(0.99), sfbunc(0.015);
    float leff(0.13), sfl(1.05), sflunc(0.12);

    // setup calibration readers 80X
    BTagCalibration btagCalib("CSVv2", string(std::getenv("CMSSW_BASE"))+"/src/UserCode/bsmhiggs_fwk/data/weights/CSVv2_Moriond17_B_H.csv");
    // setup calibration readers 80X
    BTagCalibrationReader80X btagCal80X   (BTagEntry::OP_LOOSE, "central", {"up", "down"});
    btagCal80X.load(btagCalib, BTagEntry::FLAV_B, "comb");
    btagCal80X.load(btagCalib, BTagEntry::FLAV_C, "comb");
    btagCal80X.load(btagCalib, BTagEntry::FLAV_UDSG, "incl");


    //systematics
    bool runSystematics = runProcess.getParameter<bool>("runSystematics");
    std::vector<TString> varNames(1,"");
    if(runSystematics) {
        cout << "Systematics will be computed for this analysis: " << endl;
        varNames.push_back("_jerup"); 	//1
        varNames.push_back("_jerdown"); //2
        varNames.push_back("_jesup"); 	//3
        varNames.push_back("_jesdown"); //4
        varNames.push_back("_umetup"); 	//5
        varNames.push_back("_umetdown");//6
        varNames.push_back("_lesup"); 	//7
        varNames.push_back("_lesdown"); //8
        varNames.push_back("_puup"); 	//9
        varNames.push_back("_pudown"); 	//10
        varNames.push_back("_btagup"); 	//11
        varNames.push_back("_btagdown");//12
        if(isMCBkg_runPDFQCDscale) {
            varNames.push_back("_pdfup");
            varNames.push_back("_pdfdown");
            varNames.push_back("_qcdscaleup");
            varNames.push_back("_qcdscaledown");

        }
        if(isSignal) {
            varNames.push_back("_pdfup");
            varNames.push_back("_pdfdown");
            varNames.push_back("_qcdscaleup");
            varNames.push_back("_qcdscaledown");
//            varNames.push_back("_pdfacceptup");
//            varNames.push_back("_pdfacceptdown");
//            varNames.push_back("_qcdscaleacceptup");
//            varNames.push_back("_qcdscaleacceptdown");
        }
        if(isMC_ZZ2L2Nu) {
            varNames.push_back("_qqZZewkup");
            varNames.push_back("_qqZZewkdown");
        }

        for(size_t sys=1; sys<varNames.size(); sys++) {
            cout << varNames[sys] << endl;
        }
    }
    size_t nvarsToInclude=varNames.size();


    //tree info
    int evStart     = runProcess.getParameter<int>("evStart");
    int evEnd       = runProcess.getParameter<int>("evEnd");
    TString dirname = runProcess.getParameter<std::string>("dirName");

    //jet energy scale uncertainties
    TString jecDir = runProcess.getParameter<std::string>("jecDir");
    //    gSystem->ExpandPathName(uncFile);
    cout << "Loading jet energy scale uncertainties from: " << jecDir << endl;

    if(dtag.Contains("2016B") || dtag.Contains("2016C") ||dtag.Contains("2016D")) jecDir+="Summer16_80X/Summer16_23Sep2016BCDV4_DATA/";
    else if(dtag.Contains("2016E") || dtag.Contains("2016F")) jecDir+="Summer16_80X/Summer16_23Sep2016EFV4_DATA/";
    else if(dtag.Contains("2016G")) jecDir+="Summer16_80X/Summer16_23Sep2016GV4_DATA/";
    else if(dtag.Contains("2016H")) jecDir+="Summer16_80X/Summer16_23Sep2016HV4_DATA/";
    if(isMC) {jecDir+="Summer16_80X/Summer16_23Sep2016V4_MC/";}

    gSystem->ExpandPathName(jecDir);
    FactorizedJetCorrector *jesCor = NULL;
    jesCor = utils::cmssw::getJetCorrector(jecDir,isMC);

    TString pf(isMC ? "MC" : "DATA");
    JetCorrectionUncertainty *totalJESUnc = NULL;
    totalJESUnc = new JetCorrectionUncertainty((jecDir+"/"+pf+"_Uncertainty_AK4PFchs.txt").Data());
    //    JetCorrectionUncertainty jecUnc(uncFile.Data());

    //pdf info	
    
    //##################################################################################
    //##########################    INITIATING HISTOGRAMS     ##########################
    //##################################################################################

    SmartSelectionMonitor mon;

    /*
    TH1F *h=(TH1F*) mon.addHistogram( new TH1F ("eventflow", ";;Events", 10,0,10) );
    h->GetXaxis()->SetBinLabel(1,"Trigger && 2 leptons");
    h->GetXaxis()->SetBinLabel(2,"|#it{m}_{ll}-#it{m}_{Z}|<15");

    h=(TH1F*) mon.addHistogram( new TH1F ("Acceptance", ";;Events", 3,0,3) );
    h->GetXaxis()->SetBinLabel(1,"Trigger && 2 Tight Leptons");
    h->GetXaxis()->SetBinLabel(2,"Trigger && 2 Tight Leptons && #geq 1 Leptons in MEX/1");
    h->GetXaxis()->SetBinLabel(3,"Trigger && 2 Tight Leptons && =2 Leptons in MEX/1");

    //for MC normalization (to 1/pb)
    TH1F* Hcutflow  = (TH1F*) mon.addHistogram(  new TH1F ("cutflow"    , "cutflow"    ,6,0,6) ) ;

    mon.addHistogram( new TH1F( "nvtx_raw",	";Vertices;Events",50,0,50) );
    mon.addHistogram( new TH1F( "nvtxwgt_raw",	";Vertices;Events",50,0,50) );
    mon.addHistogram( new TH1F( "zpt_raw",      ";#it{p}_{T}^{ll} [GeV];Events", 50,0,500) );
    mon.addHistogram( new TH1F( "pfmet_raw",    ";E_{T}^{miss} [GeV];Events", 50,0,500) );
    mon.addHistogram( new TH1F( "mt_raw",       ";#it{m}_{T} [GeV];Events", 100,0,2000) );
    double MTBins[]= {0,10,20,30,40,50,60,70,80,90,100,120,140,160,180,200,250,300,350,400,500,1000,2000};
    const int nBinsMT = sizeof(MTBins)/sizeof(double) - 1;
    mon.addHistogram( new TH1F( "mt2_raw",       ";#it{m}_{T} [GeV];Events", nBinsMT,MTBins) );
    mon.addHistogram( new TH1F( "zmass_raw",    ";#it{m}_{ll} [GeV];Events", 100,40,250) );

    mon.addHistogram( new TH2F( "ptlep1vs2_raw",";#it{p}_{T}^{l1} [GeV];#it{p}_{T}^{l2} [GeV];Events",250,0,500, 250,0,500) );

    mon.addHistogram( new TH1F( "leadlep_pt_raw", ";Leading lepton #it{p}_{T}^{l};Events", 50,0,500) );
    mon.addHistogram( new TH1F( "leadlep_eta_raw",";Leading lepton #eta^{l};Events", 52,-2.6,2.6) );
    mon.addHistogram( new TH1F( "trailep_pt_raw", ";Trailing lepton #it{p}_{T}^{l};Events", 50,0,500) );
    mon.addHistogram( new TH1F( "trailep_eta_raw",";Trailing lepton #eta^{l};Events", 52,-2.6,2.6) );
    */

     // RECO level
    mon.addHistogram( new TH1F( "dR",";#Delta R(b,fat-jet);Events",100,0.,5.));
    
    mon.addHistogram( new TH1F( "jet_pt_raw", ";all jet #it{p}_{T}^{j};Events", 50,0,500) );
    mon.addHistogram( new TH1F( "jet_eta_raw",";all jet #eta^{j};Events", 52,-2.6,2.6) );

    TH1F *h1 = (TH1F*) mon.addHistogram( new TH1F( "nleptons_raw", ";Lepton multiplicity;Events", 3,2,5) );
    for(int ibin=1; ibin<=h1->GetXaxis()->GetNbins(); ibin++) {
        TString label("");
        label +="=";
        label += (ibin+1);
        h1->GetXaxis()->SetBinLabel(ibin,label);
    }

    TH1F *h2 = (TH1F *)mon.addHistogram( new TH1F("njets_raw",  ";Jet multiplicity (#it{p}_{T}>30 GeV);Events",5,0,5) );
    for(int ibin=1; ibin<=h2->GetXaxis()->GetNbins(); ibin++) {
        TString label("");
        if(ibin==h2->GetXaxis()->GetNbins()) label +="#geq";
        else                                label +="=";
        label += (ibin-1);
        h2->GetXaxis()->SetBinLabel(ibin,label);
    }

    TH1F *h3 = (TH1F *)mon.addHistogram( new TH1F("nbjets_raw",    ";b-tag Jet multiplicity;Events",5,0,5) );
    for(int ibin=1; ibin<=h3->GetXaxis()->GetNbins(); ibin++) {
        TString label("");
        if(ibin==h3->GetXaxis()->GetNbins()) label +="#geq";
        else                                label +="=";
        label += (ibin-1);
        h3->GetXaxis()->SetBinLabel(ibin,label);
    }

    /*
    // preselection plots
    double METBins[]= {0,10,20,30,40,50,60,70,80,90,100,120,140,160,180,200,250,300,350,400,500};
    const int nBinsMET = sizeof(METBins)/sizeof(double) - 1;

    double METBins2[]= {0,10,20,30,40,50,60,70,80,90,100,120,140,160,180,200,250,300,350,400,500,1000};
    const int nBinsMET2 = sizeof(METBins2)/sizeof(double) - 1;

    double MET2Bins[]= {0,80,160,240,320,400,480,560,640,800,1200};
    const int xnBinsMET2 = sizeof(MET2Bins)/sizeof(double) - 1;

    double MT2Bins[]= {0,100,200,300,400,500,600,700,800,1000,1200};
    const int xnBinsMT2 = sizeof(MT2Bins)/sizeof(double) - 1;
    */

    // btaging efficiency

    //#################################################
    //############# CONTROL PLOTS #####################
    //#################################################


    //##################################################################################
    //########################## STUFF FOR CUTS OPTIMIZATION  ##########################
    //##################################################################################


    //##################################################################################
    //#############         GET READY FOR THE EVENT LOOP           #####################
    //##################################################################################

    //open the file and get events tree
    DataEvtSummaryHandler summaryHandler_;

    TFile *file = TFile::Open(url);
    printf("Looping on %s\n",url.Data());
    if(file==0) return -1;
    if(file->IsZombie()) return -1;
    if( !summaryHandler_.attachToTree( (TTree *)file->Get(dirname) ) ) {
        file->Close();
        return -1;
    }


    //check run range to compute scale factor (if not all entries are used)
    const Int_t totalEntries= summaryHandler_.getEntries();
    float rescaleFactor( evEnd>0 ?  float(totalEntries)/float(evEnd-evStart) : -1 );
    if(evEnd<0 || evEnd>summaryHandler_.getEntries() ) evEnd=totalEntries;
    if(evStart > evEnd ) {
        file->Close();
        return -1;
    }

    //MC normalization (to 1/pb)
    /*
    float cnorm=1.0;
    if(isMC) {
        //TH1F* cutflowH = (TH1F *) file->Get("mainAnalyzer/llvv/nevents");
        //if(cutflowH) cnorm=cutflowH->GetBinContent(1);
        TH1F* posH = (TH1F *) file->Get("mainAnalyzer/llvv/n_posevents");
        TH1F* negH = (TH1F *) file->Get("mainAnalyzer/llvv/n_negevents");
        if(posH && negH) cnorm = posH->GetBinContent(1) - negH->GetBinContent(1);
        if(rescaleFactor>0) cnorm /= rescaleFactor;
        printf("cnorm = %f\n",cnorm);
    }
    Hcutflow->SetBinContent(1,cnorm);
    */

    /*    
    //pileup weighting
    TString PU_Central = runProcess.getParameter<std::string>("PU_Central");
    gSystem->ExpandPathName(PU_Central);
    cout << "Loading PU weights Central: " << PU_Central << endl;
    TFile *PU_Central_File = TFile::Open(PU_Central);
    TH1F* weight_pileup_Central = (TH1F *) PU_Central_File->Get("pileup");

    TString PU_Up = runProcess.getParameter<std::string>("PU_Up");
    gSystem->ExpandPathName(PU_Up);
    cout << "Loading PU weights Up: " << PU_Up << endl;
    TFile *PU_Up_File = TFile::Open(PU_Up);
    TH1F* weight_pileup_Up = (TH1F *) PU_Up_File->Get("pileup");

    TString PU_Down = runProcess.getParameter<std::string>("PU_Down");
    gSystem->ExpandPathName(PU_Down);
    cout << "Loading PU weights Down: " << PU_Down << endl;
    TFile *PU_Down_File = TFile::Open(PU_Down);
    TH1F* weight_pileup_Down = (TH1F *) PU_Down_File->Get("pileup");
    
    */

    // muon trigger efficiency SF
    //Electron ID RECO SF


    // event categorizer
    //    EventCategory eventCategoryInst(1);   //jet(0,1,>=2) binning


    // Lepton scale factors
    LeptonEfficiencySF lepEff;

    //####################################################################################################################
    //###########################################           EVENT LOOP         ###########################################
    //####################################################################################################################


    // loop on all the events
    int treeStep = (evEnd-evStart)/50;
    if(treeStep==0)treeStep=1;
    DuplicatesChecker duplicatesChecker;
    int nDuplicates(0);
    printf("Progressing Bar     :0%%       20%%       40%%       60%%       80%%       100%%\n");
    printf("Scanning the ntuple :");

    for( int iev=evStart; iev<evEnd; iev++) {
        if((iev-evStart)%treeStep==0) {
	  printf("."); fflush(stdout);
        }

	if ( verbose ) printf("\n\n Event info %3d: \n",iev);

        //##############################################   EVENT LOOP STARTS   ##############################################
        //load the event content from tree
        summaryHandler_.getEntry(iev);
        DataEvtSummary_t &ev=summaryHandler_.getEvent();
        if(!isMC && duplicatesChecker.isDuplicate( ev.run, ev.lumi, ev.event) ) {
            nDuplicates++;
            cout << "nDuplicates: " << nDuplicates << endl;
            continue;
        }


        //prepare the tag's vectors for histo filling
        std::vector<TString> tags(1,"all");

        //genWeight
        float genWeight = 1.0;
        if(isMC && ev.genWeight<0) genWeight = -1.0;

        //systematical weight
        float weight = 1.0;
        if(isMC) weight *= genWeight;
        //only take up and down from pileup effect
        double TotalWeight_plus = 1.0;
        double TotalWeight_minus = 1.0;

        if(isMC) mon.fillHisto("pileup", "all", ev.ngenTruepu, 1.0);
	/*
        if(isMC) {
            weight 		*= getSFfrom1DHist(ev.ngenTruepu, weight_pileup_Central);
            TotalWeight_plus 	*= getSFfrom1DHist(ev.ngenTruepu, weight_pileup_Up);
            TotalWeight_minus 	*= getSFfrom1DHist(ev.ngenTruepu, weight_pileup_Down);
        }

        Hcutflow->Fill(1,genWeight);
        Hcutflow->Fill(2,weight);
        Hcutflow->Fill(3,weight*TotalWeight_minus);
        Hcutflow->Fill(4,weight*TotalWeight_plus);
	*/

        // add PhysicsEvent_t class, get all tree to physics objects
        PhysicsEvent_t phys=getPhysicsEventFrom(ev);

        // FIXME need to have a function: loop all leptons, find a Z candidate,
        // can have input, ev.mn, ev.en
        // assign ee,mm,emu channel
        // check if channel name is consistent with trigger
        // store dilepton candidate in lep1 lep2, and other leptons in 3rdleps


        bool hasMMtrigger = ev.triggerType & 0x1;
        bool hasMtrigger  = (ev.triggerType >> 1 ) & 0x1;
        bool hasEEtrigger = (ev.triggerType >> 2 ) & 0x1;
	// type 3 is high-pT eeTrigger (safety)
        bool hasEtrigger  = (ev.triggerType >> 4 ) & 0x1;
        bool hasEMtrigger = (ev.triggerType >> 5 ) & 0x1;


        //#########################################################################
        //#####################      Objects Selection       ######################
        //#########################################################################

        //
        // MET ANALYSIS
        //
        //apply Jet Energy Resolution corrections to jets (and compute associated variations on the MET variable)
        //std::vector<PhysicsObjectJetCollection> variedJets;
        //LorentzVectorCollection variedMET;

	//        METUtils::computeVariation(phys.jets, phys.leptons, (usemetNoHF ? phys.metNoHF : phys.met), variedJets, variedMET, &jecUnc);

        LorentzVector metP4=phys.met; //variedMET[0];
        PhysicsObjectJetCollection &corrJets = phys.jets; //variedJets[0];
	PhysicsObjectFatJetCollection &fatJets = phys.fatjets;

        //
        // LEPTON ANALYSIS
        //

        // looping leptons (electrons + muons)
        int nGoodLeptons(0);
        std::vector<std::pair<int,LorentzVector> > goodLeptons;
        for(size_t ilep=0; ilep<phys.leptons.size(); ilep++) {
            LorentzVector lep=phys.leptons[ilep];
            int lepid = phys.leptons[ilep].id;
            if(lep.pt()<25) continue;
            if(abs(lepid)==13 && fabs(lep.eta())> 2.4) continue;
            if(abs(lepid)==11 && fabs(lep.eta())> 2.5) continue;
            if(abs(lepid)==11 && fabs(lep.eta()) > 1.442 && fabs(lep.eta()) < 1.556) continue;

            bool hasTightIdandIso(true);
            if(abs(lepid)==13) { //muon
	      hasTightIdandIso &= (phys.leptons[ilep].passIdMu && phys.leptons[ilep].passIsoMu);
                //https://twiki.cern.ch/twiki/bin/viewauth/CMS/SWGuideMuonIdRun2?sortcol=1;table=7;up=0#Muon_Isolation
	      //                hasTightIdandIso &= ( phys.leptons[ilep].m_pfRelIsoDbeta() < 0.15 );
            } else if(abs(lepid)==11) { //electron
	      hasTightIdandIso &= (phys.leptons[ilep].passIdEl && phys.leptons[ilep].passIsoEl);
            } else continue;


            if(!hasTightIdandIso) continue;
            nGoodLeptons++;
            std::pair <int,LorentzVector> goodlep;
            goodlep = std::make_pair(lepid,lep);
            goodLeptons.push_back(goodlep);

        }

	// sort goodLeptons in pT
	//	sort(goodLeptons.begin(), goodLeptons.end(), ptsort());
	//	mon.fillHisto("nleptons_raw","nlep", goodLeptons.second.size(),weight);
	
	//	if(nGoodLeptons<1) continue; // at least 1 tight leptons
	/*
        float _MASSDIF_(999.);
        int id1(0),id2(0);
        LorentzVector lep1(0,0,0,0),lep2(0,0,0,0);
        for(size_t ilep=0; ilep<goodLeptons.size(); ilep++) {
            int id1_ = goodLeptons[ilep].first;
            LorentzVector lep1_ = goodLeptons[ilep].second;

            for(size_t jlep=ilep+1; jlep<goodLeptons.size(); jlep++) {
                int id2_ = goodLeptons[jlep].first;
                LorentzVector lep2_ = goodLeptons[jlep].second;
                if(id1_*id2_>0) continue; // opposite charge

                LorentzVector dilepton=lep1_+lep2_;
                double massdif = fabs(dilepton.mass()-91.);
                if(massdif < _MASSDIF_) {
                    _MASSDIF_ = massdif;
                    lep1.SetPxPyPzE(lep1_.px(),lep1_.py(),lep1_.pz(),lep1_.energy());
                    lep2.SetPxPyPzE(lep2_.px(),lep2_.py(),lep2_.pz(),lep2_.energy());
                    id1 = id1_;
                    id2 = id2_;
                }
            }
        }

	*/
	/*
        // ID + ISO scale factors (only muons for the time being)
        // Need to implement variations for errors (unused for now)
        if(isMC) {
         
        }
	*/


	/*
        if(id1*id2==0) continue;
        LorentzVector zll(lep1+lep2);
        bool passZmass(fabs(zll.mass()-91)<10);
        bool passZpt(zll.pt()>50);
	*/

        TString tag_cat;
        int evcat=-1;
	if (goodLeptons.size()==1) evcat = getLeptonId(abs(goodLeptons[0].first));
	if (goodLeptons.size()>1) evcat = getDileptonId(abs(goodLeptons[0].first),abs(goodLeptons[1].first)); 
        switch(evcat) {
        case MUMU :
            tag_cat = "mumu";
            break;
        case EE   :
            tag_cat = "ee";
            break;
	case MU  :
	    tag_cat = "mu";
	    break;
	case E   :
	    tag_cat = "e";
	    break;
        case EMU  :
            tag_cat = "emu";
            break;
	    //    default   :
	    // continue;
        }
	/*
        //split inclusive DY sample into DYToLL and DYToTauTau
        if(isMC && mctruthmode==1) {
            //if(phys.genleptons.size()!=2) continue;
            if(phys.genleptons.size()==2 && isDYToTauTau(phys.genleptons[0].id, phys.genleptons[1].id) ) continue;
        }

        if(isMC && mctruthmode==2) {
            if(phys.genleptons.size()!=2) continue;
            if(!isDYToTauTau(phys.genleptons[0].id, phys.genleptons[1].id) ) continue;
        }
	*/
	/*
        bool hasTrigger(false);
	
        if(!isMC) {
            if(evcat!=fType) continue;

            if(evcat==EE   && !(hasEEtrigger||hasEtrigger) ) continue;
            if(evcat==MUMU && !(hasMMtrigger||hasMtrigger) ) continue;
            if(evcat==EMU  && !hasEMtrigger ) continue;

            //this is a safety veto for the single mu PD
            if(isSingleMuPD) {
                if(!hasMtrigger) continue;
                if(hasMtrigger && hasMMtrigger) continue;
            }
            if(isDoubleMuPD) {
                if(!hasMMtrigger) continue;

            }

            //this is a safety veto for the single Ele PD
            if(isSingleElePD) {
                if(!hasEtrigger) continue;
                if(hasEtrigger && hasEEtrigger) continue;
            }
            if(isDoubleElePD) {
                if(!hasEEtrigger) continue;
            }

            hasTrigger=true;

        } else {
	  if( evcat==EE   && (hasEEtrigger || hasEtrigger) ) hasTrigger=true;
            if(evcat==MUMU && (hasMMtrigger || hasMtrigger) ) hasTrigger=true;
            if(evcat==EMU  && hasEMtrigger ) hasTrigger=true;
            if(!hasTrigger) continue;
        }
	*/
        tags.push_back(tag_cat); //add ee, mumu, emu category

        // pielup reweightiing
        mon.fillHisto("nvtx_raw",   tags, phys.nvtx,      1.0);
        //if(isMC) weight *= myWIMPweights.get1DWeights(phys.nvtx,"pileup_weights");
        mon.fillHisto("nvtxwgt_raw",tags, phys.nvtx,      weight);

        //
        //apply muon trigger efficiency scale factors
        //

        //
        //JET AND BTAGGING ANALYSIS
        //

	//###########################################################
	//  AK4 jets ,
	// AK4 jets + CSVloose b-tagged configuration
	//###########################################################
	
        PhysicsObjectJetCollection GoodIdJets, GoodIdJets_true;
	PhysicsObjectJetCollection CSVLoosebJets, CSVLoosebJets_true;

        int nJetsGood30(0);
        int nCSVLtags(0),nCSVMtags(0),nCSVTtags(0);
        double BTagWeights(1.0);
        for(size_t ijet=0; ijet<corrJets.size(); ijet++) {

            if(corrJets[ijet].pt()<20) continue;
            if(fabs(corrJets[ijet].eta())>4.7) continue;

            //jet ID
            if(!corrJets[ijet].isPFLoose) continue;
            //if(corrJets[ijet].pumva<0.5) continue;

            //check overlaps with selected leptons
            double minDR(999.);
	    for(size_t ilep=0; ilep<goodLeptons.size(); ilep++) {
                double dR = deltaR( corrJets[ijet], goodLeptons[ilep].second );
                if(dR > minDR) continue;
                minDR = dR;
            }
            if(minDR < 0.4) continue;

            GoodIdJets.push_back(corrJets[ijet]);
	    if (corrJets[ijet].motherid == 36) GoodIdJets_true.push_back(corrJets[ijet]);
            if(corrJets[ijet].pt()>30) nJetsGood30++;


            //https://twiki.cern.ch/twiki/bin/viewauth/CMS/BtagRecommendation80X
            if(corrJets[ijet].pt()>20 && fabs(corrJets[ijet].eta())<2.4)  {

                nCSVLtags += (corrJets[ijet].btag0>CSVLooseWP);
                nCSVMtags += (corrJets[ijet].btag0>CSVMediumWP);
                nCSVTtags += (corrJets[ijet].btag0>CSVTightWP);

                bool hasCSVtag(corrJets[ijet].btag0>CSVLooseWP);
		if (isMC) {
		  // Apply b-tag SFs with Moriond17 recommendations (2016 data):
		  btsfutil.SetSeed(ev.event*10 + ijet*10000);

		  if(abs(corrJets[ijet].flavid)==5) {
		    //  80X recommendation
		    btsfutil.modifyBTagsWithSF(hasCSVtag , btagCal80X.eval_auto_bounds("central", BTagEntry::FLAV_B , corrJets[ijet].eta(), corrJets[ijet].pt()), beff);
		  } else if(abs(corrJets[ijet].flavid)==4) {
		    //  80X recommendation
		    btsfutil.modifyBTagsWithSF(hasCSVtag , btagCal80X.eval_auto_bounds("central", BTagEntry::FLAV_C , corrJets[ijet].eta(), corrJets[ijet].pt()), beff);
		  } else {
		    //  80X recommendation
		    btsfutil.modifyBTagsWithSF(hasCSVtag , btagCal80X.eval_auto_bounds("central", BTagEntry::FLAV_UDSG , corrJets[ijet].eta(), corrJets[ijet].pt()), leff);
		  }
		} // isMC
		
		// Fill b-jet vector:
		if (hasCSVtag) {
		  CSVLoosebJets.push_back(corrJets[ijet]);
		  if (corrJets[ijet].motherid == 36) CSVLoosebJets_true.push_back(corrJets[ijet]);
		}

            } // b-jet loop
        } // jet loop

	
	//###########################################################
	// Now AK8 fat jets configuration
	//###########################################################
	
	// AK8 + double-b tagger fat-jet collection
	PhysicsObjectFatJetCollection DBfatJets, DBfatJets_true; // collection of AK8 fat jets

	int ifjet(0);
	//	for(size_t ijet=0; ijet<fatJets.size(); ijet++) {
	for (auto & ijet : fatJets ) {
	
	  if(ijet.pt()<20) continue;
	  if(fabs(ijet.eta())>2.4) continue;

	  ifjet++;

	  int count_sbj(0);
	    // Examine soft drop subjets in AK8 jet:
	  count_sbj = ijet.subjets.size(); // count subjets above 20 GeV only
	  
	  if ( verbose ) printf("\n\n Print info for subjets in AK8 %3d : ", ifjet);

	  if ( verbose ) {
	    // loop over subjets of AK8 jet
	    for (auto & it : ijet.subjets ) {
	      printf("\n subjet in Ntuple has : pt=%6.1f, eta=%7.3f, phi=%7.3f, mass=%7.3f",   
		     it.Pt(),
		     it.Eta(),
		     it.Phi(),
		     it.M()
		     );
	    }
	    printf("\n\n");
	  } // verbose

	  bool hasDBtag(ijet.btag0>DBLooseWP);
	  
	  if (hasDBtag && count_sbj>0) {
	    // double-b tagger + at least 1 subjet in AK8
	    DBfatJets.push_back(ijet);
	    if (ijet.motherid == 36) DBfatJets_true.push_back(ijet);
	  }
	  
	} // AK8 fatJets loop
	
	//###########################################################
	//  Configure cleaned Ak4 and AK4 + CSVloose Jets : 
	//       Require DR separation with AK8 subjets
	//###########################################################

	PhysicsObjectJetCollection cleanedGoodIdJets, cleanedGoodIdJets_true;
	PhysicsObjectJetCollection cleanedCSVLoosebJets, cleanedCSVLoosebJets_true;
	
	for (auto & ib : CSVLoosebJets) {

	  bool hasOverlap(0);
	  
	  // Loop over AK8 jets and find subjets
	  for (auto & ifb : DBfatJets) {
	    for (auto & it : ifb.subjets) { // subjets loop
	      hasOverlap = (deltaR(ib, it)<0.4);

	      if ( verbose ) {
		if (hasOverlap) {

		  printf("\n Found overlap of b-jet : pt=%6.1f, eta=%7.3f, phi=%7.3f, mass=%7.3f \n with subjet in AK8: pt=%6.1f, eta=%7.3f, phi=%7.3f, mass=%7.3f \n",
			 ib.Pt(),
			 ib.Eta(),
			 ib.Phi(),
			 ib.M(),

			 it.Pt(),
			 it.Eta(),
			 it.Phi(),
			 it.M()
			 );
		  
		} // hasOverlap
	      } //verbose
	      
	    } // subjets loop
	  } // AK8 loop

	  if (!hasOverlap) {
	    cleanedCSVLoosebJets.push_back(ib);
	    if (ib.motherid == 36) cleanedCSVLoosebJets_true.push_back(ib);
	  }
	} // CSV b-jet loop

	//--------------------------------------------------------------------------
	//--------------------------------------------------------------------------
	// some strings for plotting purposes:
	const char* astr[] = {"_b1","_b2","_b3","_b4"};
        std::vector<TString> mybs(astr, astr+4);

	//--------------------------------------------------------------------------
	// AK4 jets:
	sort(GoodIdJets.begin(), GoodIdJets.end(), ptsort());
	sort(GoodIdJets_true.begin(), GoodIdJets_true.end(), ptsort());

	// Fill Histograms with AK4,AK4 + CVS, AK8 + db basics:
	mon.fillHisto("njets_raw","nj", GoodIdJets.size(),weight);
	mon.fillHisto("njets_raw","nj_true", GoodIdJets_true.size(),weight);

	int is(0);
	for (auto & i : GoodIdJets) {
	   mon.fillHisto("jet_pt_raw", "nj"+mybs[is], i.pt(),weight);
	   mon.fillHisto("jet_eta_raw", "nj"+mybs[is], i.eta(),weight);
	   is++;
	   if (is>3) break; // plot only up to 4 b-jets ?
	}
	is=0;
	for (auto & i : GoodIdJets_true) {
	   mon.fillHisto("jet_pt_raw", "nj_true"+mybs[is], i.pt(),weight);
	   mon.fillHisto("jet_eta_raw", "nj_true"+mybs[is], i.eta(),weight);
	   is++;
	   if (is>3) break; // plot only up to 4 b-jets ?
	}
	
	// AK4 + CSV jets:
	sort(CSVLoosebJets.begin(), CSVLoosebJets.end(), ptsort());
	sort(CSVLoosebJets_true.begin(), CSVLoosebJets_true.end(), ptsort());
	
	mon.fillHisto("nbjets_raw","nb", CSVLoosebJets.size(),weight);
	mon.fillHisto("nbjets_raw","nb_true", CSVLoosebJets_true.size(),weight);

	is=0;
	for (auto & i : CSVLoosebJets) {
	   mon.fillHisto("jet_pt_raw", "nb"+mybs[is], i.pt(),weight);
	   mon.fillHisto("jet_eta_raw", "nb"+mybs[is], i.eta(),weight);
	   is++;
	   if (is>3) break; // plot only up to 4 b-jets ?
	}
	is=0;
	for (auto & i : CSVLoosebJets_true) {
	   mon.fillHisto("jet_pt_raw", "nb_true"+mybs[is], i.pt(),weight);
	   mon.fillHisto("jet_eta_raw", "nb_true"+mybs[is], i.eta(),weight);
	   is++;
	   if (is>3) break; // plot only up to 4 b-jets ?
	}

	//--------------------------------------------------------------------------
	//--------------------------------------------------------------------------

	// AK8 + double-b jets
	sort(DBfatJets.begin(), DBfatJets.end(), ptsort());
	sort(DBfatJets_true.begin(), DBfatJets_true.end(), ptsort());

	mon.fillHisto("nbjets_raw","nfatJet", DBfatJets.size(),weight);
	mon.fillHisto("nbjets_raw","nfatJet_true", DBfatJets_true.size(),weight);

	is=0;
	for (auto & i : DBfatJets) {
	   mon.fillHisto("jet_pt_raw", "nfat"+mybs[is], i.pt(),weight);
	   mon.fillHisto("jet_eta_raw", "nfat_"+mybs[is], i.eta(),weight);
	   is++;
	   if (is>3) break; // plot only up to 4 b-jets ?
	}
	is=0;
	for (auto & i : DBfatJets_true) {
	   mon.fillHisto("jet_pt_raw", "nfat_true"+mybs[is], i.pt(),weight);
	   mon.fillHisto("jet_eta_raw", "nfat_true"+mybs[is], i.eta(),weight);
	   is++;
	   if (is>3) break; // plot only up to 4 b-jets ?
	}
	
	// Cross-cleaned AK4 CSV b-jets:
	sort(cleanedCSVLoosebJets.begin(), cleanedCSVLoosebJets.end(), ptsort());
	sort(cleanedCSVLoosebJets_true.begin(), cleanedCSVLoosebJets_true.end(), ptsort());

	mon.fillHisto("nbjets_raw","nb_cleaned", cleanedCSVLoosebJets.size(),weight);
	mon.fillHisto("nbjets_raw","nb_cleaned_true", cleanedCSVLoosebJets_true.size(),weight);

	is=0;
	for (auto & i : cleanedCSVLoosebJets) {
	   mon.fillHisto("jet_pt_raw", "nb_cleaned"+mybs[is], i.pt(),weight);
	   mon.fillHisto("jet_eta_raw", "nb_cleaned"+mybs[is], i.eta(),weight);
	   is++;
	   if (is>3) break; // plot only up to 4 b-jets ?
	}
	is=0;
	for (auto & i : cleanedCSVLoosebJets_true) {
	   mon.fillHisto("jet_pt_raw", "nb_cleaned_true"+mybs[is], i.pt(),weight);
	   mon.fillHisto("jet_eta_raw", "nb_cleaned_true"+mybs[is], i.eta(),weight);
	   is++;
	   if (is>3) break; // plot only up to 4 b-jets ?
	}
	
	//--------------------------------------------------------------------------
	//--------------------------------------------------------------------------
	// minDR between a b-jet and AK8 jet

	int ibs(0);
	for (auto & ib : CSVLoosebJets) {
	  
	  float dRmin(999.);
	  float dRmin_sub(999.);
	  
	  for (auto & it : DBfatJets) {
	    float dR = deltaR(ib, it);
	    if (dR<dRmin) dRmin=dR;

	    // loop in subjets
	    for (auto & isub : it.subjets){
	      float dRsub = deltaR(ib, isub);
	      if (dRsub<dRmin_sub) dRmin_sub=dRsub;
	    }
	  }
	  mon.fillHisto("dR","drmin"+mybs[ibs],dRmin, weight);
	  mon.fillHisto("dR","drmin_sub"+mybs[ibs],dRmin_sub, weight);

	  ibs++;
	  if (ibs>3) break; // plot only up to 4 b-jets ?
	} 


	// DR separation between true b's and true fatJets
	ibs=0;
	for (auto & ib : CSVLoosebJets_true) {
	  
	  float dRmin(999.);
	  float dRmin_sub(999.);
	  
	  for (auto & it : DBfatJets_true) {
	    float dR = deltaR(ib, it);
	    if (dR<dRmin) dRmin=dR;

	    // loop in subjets
	    for (auto & isub : it.subjets){
	      float dRsub = deltaR(ib, isub);
	      if (dRsub<dRmin_sub) dRmin_sub=dRsub;
	    }
	  }
	  mon.fillHisto("dR","drmin_true"+mybs[ibs],dRmin, weight);
	  mon.fillHisto("dR","drmin_sub_true"+mybs[ibs],dRmin_sub, weight);

	  ibs++;
	  if (ibs>3) break; // plot only up to 4 b-jets ?
	}
	
        //#########################################################
        //####  RUN PRESELECTION AND CONTROL REGION PLOTS  ########
        //#########################################################


        //##############################################
        //########  Main Event Selection        ########
        //##############################################


        //##############################################################################
        //### HISTOS FOR STATISTICAL ANALYSIS (include systematic variations)
        //##############################################################################


	//##############################################
	// recompute MET/MT if JES/JER was varied
	//##############################################
	//LorentzVector vMET = variedMET[ivar>8 ? 0 : ivar];
	//PhysicsObjectJetCollection &vJets = ( ivar<=4 ? variedJets[ivar] : variedJets[0] );
	


    } // loop on all events END


    printf("\n");
    file->Close();

    //##############################################
    //########     SAVING HISTO TO FILE     ########
    //##############################################
    //save control plots to file
    outUrl += "/";
    outUrl += outFileUrl + ".root";
    printf("Results saved in %s\n", outUrl.Data());

    //save all to the file
    TFile *ofile=TFile::Open(outUrl, "recreate");
    mon.Write();
    ofile->Close();

    if(outTxtFile_final)fclose(outTxtFile_final);
}

