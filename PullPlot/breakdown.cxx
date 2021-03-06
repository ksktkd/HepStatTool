// Author      : Stefan Gadatsch
// Email       : gadatsch@nikhef.nl
// Date        : 2013-04-26
// Description : Compute uncertainty due to different groups of parameters specified in a XML

#include <string>
#include <vector>

#include "TFile.h"
#include "TH1D.h"
#include "Math/MinimizerOptions.h"
#include "TStopwatch.h"
#include "TXMLEngine.h"

#include "RooWorkspace.h"
#include "RooStats/ModelConfig.h"
#include "RooDataSet.h"
#include "RooArgSet.h"
#include "RooRealVar.h"
#include "RooNLLVar.h"
#include "RooFitResult.h"

#include "findSigma.cxx"

using namespace std;
using namespace RooFit;
using namespace RooStats;

struct settings {
    string inFileName;
    string wsName;
    string modelConfigName;
    string dataName;
    string poiName;
    string xmlName;
    string technique;
    string catecory2eval;
    double precision;
    double corrCutoff;
    bool useMinos;
    string folder;
    string loglevel;
};

void setParams(RooArgSet* nuis, list<string> tmp_list, string technique, RooFitResult* fitresult, RooRealVar* poi, double corrCutoff);
list<string> addParams(settings* config, string catecory2eval);
void writeTmpXML (string systName, std::string xmlName);

// ____________________________________________________________________________|__________
// Compute ranking of systematics specified in xml
void breakdown(
        std::string inFileName      = "1200GeV_WSMaker_workspace.root",
        std::string wsName          = "combined",
        std::string modelConfigName = "ModelConfig",
        std::string dataName        = "obsData",
        std::string poiName         = "SigXsecOverSM",
        std::string xmlName         = "config/breakdown.xml",
        std::string technique       = "add",
        std::string catecory2eval   = "total",
        double precision            = 0.005,
        double corrCutoff           = 0.0,
        std::string folder          = "12.TT.10_otT_0",
        std::string loglevel        = "DEBUG")
{
    // store all settings for passing to other functions
    settings* config = new settings();
    config->inFileName = inFileName;
    config->wsName = wsName;
    config->modelConfigName = modelConfigName;
    config->dataName = dataName;
    config->poiName = poiName;
    config->xmlName = xmlName;
    config->technique = technique;
    config->catecory2eval = catecory2eval;
    config->precision = precision;
    config->corrCutoff = corrCutoff;
    config->folder = folder;
    config->loglevel = loglevel;

    // some settings
    ROOT::Math::MinimizerOptions::SetDefaultMinimizer("Minuit2");
    ROOT::Math::MinimizerOptions::SetDefaultStrategy(1);
    ROOT::Math::MinimizerOptions::SetDefaultPrintLevel(-1);
    RooMsgService::instance().setGlobalKillBelow(RooFit::FATAL);

    // loading the workspace etc.
    std::cout << "Running over workspace: " << config->inFileName << std::endl;

    TFile* file = new TFile(config->inFileName.c_str());
    RooWorkspace* ws = (RooWorkspace*)file->Get(config->wsName.c_str());
    if (!ws) {
        std::cout << "Workspace: " << config->wsName << " doesn't exist!" << std::endl;
        exit(1);
    }

    ModelConfig* mc = (ModelConfig*)ws->obj(config->modelConfigName.c_str());
    if (!mc) {
        std::cout << "ModelConfig: " << config->modelConfigName << " doesn't exist!" << std::endl;
        exit(1);
    }

    TString datastring(dataName);
    int commaPos = datastring.Index(",");
    if(commaPos != TString::kNPOS) { 
        ws->loadSnapshot(TString(datastring(commaPos+1, datastring.Length())));
        datastring = datastring(0, commaPos);
    }

    RooDataSet* data = (RooDataSet*)ws->data(datastring);
    if (!data) {
        std::cout <<  "Dataset: " << config->dataName << " doesn't exist!" << std::endl;
        exit(1);
    }

    std::vector<RooRealVar*> pois;
    RooArgSet* POIS_detected = (RooArgSet*) mc->GetParametersOfInterest();
    TIterator* POI_itr = POIS_detected->createIterator();
    while ( RooRealVar* poi = (RooRealVar*) POI_itr->Next()) {
        if (!poi) {
            std::cout << "POI: doesn't exist!" << std::endl;
            exit(1);
        }
        poi->setVal(1);
        poi->setRange(-10.,10.);
        poi->setConstant(1);
        pois.push_back(poi);
    }

    RooArgSet* nuis = (RooArgSet*)mc->GetNuisanceParameters();
    if (!nuis) {
        std::cout << "Nuisance parameter set doesn't exist!" << std::endl;
        exit(1);
    }
    TIterator* nitr = nuis->createIterator();
    RooRealVar* var;

    RooArgSet* globs = (RooArgSet*)mc->GetGlobalObservables();
    if (!globs) {
        std::cout << "GetGlobal observables don't exist!" << std::endl;
        exit(1);
    }

    ws->loadSnapshot("nominalNuis");
    for (unsigned int i = 0; i < pois.size(); i++) {
        pois[i]->setRange(-10., 10.);
        pois[i]->setConstant(0);
        pois[i]->setVal(1.1); // Kick !
    }

    int numCPU = 4; 
    RooNLLVar* nll = (RooNLLVar*)mc->GetPdf()->createNLL(*data, Constrain(*nuis), GlobalObservables(*globs), Offset(1), NumCPU(numCPU, RooFit::Hybrid), Optimize(2));

    RooFitResult* fitresult=nullptr;
//    Fits::minimize(nll);
    RooMinuit(*nll).migrad(); 
    ROOT::Math::MinimizerOptions::SetDefaultStrategy(1);

    RooArgSet nuisAndPOI(*mc->GetNuisanceParameters(), *mc->GetParametersOfInterest());
    ws->saveSnapshot("tmp_shot", nuisAndPOI);

    double nll_val_true = nll->getVal();
    std::vector<double> pois_hat;
    for (unsigned int i = 0; i < pois.size(); i++) {
        pois_hat.push_back(pois[i]->getVal());
    }

    std::vector<double> pois_up;
    std::vector<double> pois_down;

    if (config->catecory2eval == "total") {
        for (unsigned int i = 0; i < pois.size(); i++) {
            ws->loadSnapshot("tmp_shot"); pois_up  .push_back(findSigma(nll, nll_val_true, pois[i], pois_hat[i], +1));
            ws->loadSnapshot("tmp_shot"); pois_down.push_back(findSigma(nll, nll_val_true, pois[i], pois_hat[i], -1));
        }
    }

    for (unsigned int i = 0; i < pois.size(); i++) {
        std::cout << config->catecory2eval << " gives " << pois[i]->GetName() << " = " << pois_hat[i] << " +" << pois_up[i] << " / -" << pois_down[i] << std::endl;
    }

    std::system(("mkdir -vp output/" +  string(config->folder) + "/root-files/breakdown_" + string(technique)).c_str());
    std::stringstream fileName;
    fileName << "output/" << config->folder << "/root-files/breakdown_" << technique << "/" << config->catecory2eval << ".root";
    TFile fout(fileName.str().c_str(), "recreate");


    TH1D* h_out = new TH1D(config->catecory2eval.c_str(), config->catecory2eval.c_str(), 3 * pois.size(), 0, 3 * pois.size());
    int bin = 1;
    for (unsigned int i = 0; i < pois.size(); i++) {
        h_out->SetBinContent(bin, pois_hat[i]);
        h_out->SetBinContent(bin+1, fabs(pois_up[i]));
        h_out->SetBinContent(bin+2, fabs(pois_down[i]));

        h_out->GetXaxis()->SetBinLabel(bin, pois[i]->GetName());
        h_out->GetXaxis()->SetBinLabel(bin+1, "poi_up");
        h_out->GetXaxis()->SetBinLabel(bin+2, "poi_down");
        bin += 3;
    }

    fout.Write();
    fout.Close();
}

// ____________________________________________________________________________|__________
// Set parameters constant or floating, depending on technique
void setParams(RooArgSet* nuis, list<string> tmp_list, string technique, RooFitResult* fitresult, RooRealVar* poi, double corrCutoff) {
    RooRealVar* var;
    TIterator* nitr = nuis->createIterator();

    while ((var = (RooRealVar*)nitr->Next())) {
        string varName = string(var->GetName());

        if (technique.find("sub") != string::npos) {
            var->setConstant(0);
        } else {
            var->setConstant(1);
        }

        bool found = 0;
        if (find(tmp_list.begin(), tmp_list.end(), varName) != tmp_list.end()) {
            std::cout << "Found " << varName << std::endl;
            found = 1;
        }

        if (found) {
            if (technique.find("sub") != string::npos) {
                var->setConstant(1);
            } else {
                var->setConstant(0);
            }
        }

        std::cout << varName << " is constant -> " << var->isConstant() << std::endl;

        double correlation = fitresult->correlation(varName.c_str(), poi->GetName());

        std::cout <<  "Correlation between poi and " << varName << " is " << correlation << std::endl;

        if (abs(correlation) < corrCutoff) {
            std::cout << "Setting " << varName << " constant because it's not correlated to the POI." << std::endl;
            var->setConstant(1);
        }
    }
}

// ____________________________________________________________________________|__________
// Add parameters to a list of nuisance parameters
list<string> addParams(settings* config, string catecory2eval) {
    list<string> tmp_list;

    TXMLEngine* xml = new TXMLEngine;
    XMLDocPointer_t xmldoc = xml->ParseFile(config->xmlName.c_str());
    if (!xmldoc) {
        std::cout << "XML " << config->xmlName << " doesn't exist!" << std::endl;
        exit(1);
    }

    XMLNodePointer_t mainnode = xml->DocGetRootElement(xmldoc);
    XMLNodePointer_t category = xml->GetChild(mainnode);

    while (category != 0) {
        string categoryName = xml->GetNodeName(category);

        if (categoryName.find(catecory2eval) == string::npos) {
            std::cout << "skipping " << categoryName << std::endl;
            category = xml->GetNext(category);
        } else {
            bool isBreakdown = (string(xml->GetAttr(category, "breakdown")).find("yes") != string::npos) ? 1 : 0;

            XMLNodePointer_t systematic = xml->GetChild(category);

            if (config->technique.find("add") != string::npos && catecory2eval.find("statistical") == string::npos) {
                std::cout << "Adding statistical parameters" << std::endl;
                tmp_list = addParams(config, "statistical");
            }

            while (systematic != 0) {
                XMLAttrPointer_t attr_syst = xml->GetFirstAttr(systematic);    
                while (attr_syst != 0) {
                    string systName = xml->GetAttrValue(attr_syst);

                    tmp_list.push_back(systName);

                    if (isBreakdown) {
                        std::cout << "Doing breakdown: " << systName << std::endl;
                        writeTmpXML(systName, config->xmlName);
                        breakdown(config->inFileName, config->wsName, config->modelConfigName, config->dataName, config->poiName, "config/tmp_"+systName+".xml", config->technique, systName, config->precision, config->corrCutoff, config->folder, config->loglevel);
                    }
                    attr_syst = xml->GetNextAttr(attr_syst);  
                }
                systematic = xml->GetNext(systematic);
            }
            category = xml->GetNext(category);
        }
    }
    return tmp_list;
}

// ___________________________________________________________________________|__________
// Write temporary XML for a single parameter
void writeTmpXML (string systName, std::string xmlName ) {
    // add the interesting category
    TXMLEngine* xml = new TXMLEngine;

    XMLNodePointer_t mainnode = xml->NewChild(0, 0, "breakdown");
    XMLAttrPointer_t description_main = xml->NewAttr(mainnode, 0, "description", "map of tmp uncertainties");

    XMLNodePointer_t child = xml->NewChild(mainnode, 0, systName.c_str());
    XMLAttrPointer_t description_child = xml->NewAttr(child, 0, "description", systName.c_str());
    XMLAttrPointer_t breakdown_child = xml->NewAttr(child, 0, "breakdown", "no");
    XMLAttrPointer_t scan_child = xml->NewAttr(child, 0, "scan", "no");

    XMLNodePointer_t subchild = xml->NewChild(child, 0, "systematic");
    XMLAttrPointer_t name_subchild = xml->NewAttr(subchild, 0, "name", systName.c_str());

    // add the statistical parameters as defined in top level xml
    XMLNodePointer_t child_stat = xml->NewChild(mainnode, 0, "statistical");
    XMLAttrPointer_t description_child_stat = xml->NewAttr(child_stat, 0, "description", "statistical uncertainties");
    XMLAttrPointer_t breakdown_child_stat = xml->NewAttr(child_stat, 0, "breakdown", "no");
    XMLAttrPointer_t scan_child_stat = xml->NewAttr(child_stat, 0, "scan", "no");

    TXMLEngine* xml_top = new TXMLEngine;
    XMLDocPointer_t xmldoc_top = xml_top->ParseFile(xmlName.c_str());

    XMLNodePointer_t mainnode_top = xml_top->DocGetRootElement(xmldoc_top);
    XMLNodePointer_t category_top = xml_top->GetChild(mainnode_top);

    while (category_top != 0) {
        string categoryName = xml_top->GetNodeName(category_top);

        if (categoryName.find("statistical") != string::npos) {
            xml->AddChild(child_stat, xml->GetChild(category_top));
        }
        category_top = xml_top->GetNext(category_top);
    }

    // save the new tmp xml
    XMLDocPointer_t xmldoc = xml->NewDoc();
    xml->DocSetRootElement(xmldoc, mainnode);

    xml->SaveDoc(xmldoc, ("config/tmp_"+systName+".xml").c_str());

    xml->FreeDoc(xmldoc);
    delete xml;
}
