// headless render of Six Sines patches: notes C3+G3 (48, 55) for 2 s, then 1.5 s release; prints stats
#include "synth/synth.h"
#include <fstream>
#include <sstream>
#include <iostream>
#include <cmath>
#include <vector>
namespace ss = baconpaul::six_sines;
static bool qp(const clap_output_events *, const clap_event_header_t *) { return true; }
int main(int argc, char **argv) {
  clap_host h{}; h.clap_version = CLAP_VERSION; h.name="r"; h.vendor="r"; h.url=""; h.version="1";
  h.get_extension=[](const clap_host*, const char*)->const void*{return nullptr;};
  h.request_restart=[](const clap_host*){}; h.request_process=[](const clap_host*){}; h.request_callback=[](const clap_host*){};
  clap_output_events qo{nullptr, &qp};
  const double sr = 48000;
  int keyA = getenv("KEY") ? atoi(getenv("KEY")) : 48; bool chord = getenv("CHORD") != nullptr; const char* outdir = getenv("OUT");
  for (int a = 1; a < argc; ++a) {
    std::string path = argv[a]; if (path.find(".sxsnp") == std::string::npos) continue;
    std::ifstream f(path); std::stringstream b; b << f.rdbuf();
    auto e = std::make_unique<ss::Synth>(false); e->clapHost = &h;
    e->patchMain.fromState(b.str());
    e->patch.copyValuesFrom(e->patchMain);
    e->setSampleRate(sr);
    ss::Synth::sendEntirePatchToAudio(e->patchMain, e->mainToAudio, &h);
    const int total = (int)(sr * 4.0), on = (int)(0.02*sr), off = (int)(2.0*sr);
    std::vector<float> L(total), R(total); size_t bp = 0;
    for (int s = 0; s < total; ++s) {
      if (bp == 0) {
        if (s >= on && s < on + (int)e->blockSize) { e->voiceManager->processNoteOnEvent(0,0,keyA,-1,0.8f,0.f); if(chord) e->voiceManager->processNoteOnEvent(0,0,keyA+7,-1,0.8f,0.f);}  
        if (s >= off && s < off + (int)e->blockSize) { e->voiceManager->processNoteOffEvent(0,0,keyA,-1,0.f); if(chord) e->voiceManager->processNoteOffEvent(0,0,keyA+7,-1,0.f);}  
        e->process(&qo);
      }
      L[s] = e->output[0][bp]; R[s] = e->output[1][bp];
      if (++bp == e->blockSize) bp = 0;
    }
    if (outdir) { std::string base = path.substr(path.rfind('/')+1); std::string cat = path.substr(0, path.rfind('/')); cat = cat.substr(cat.rfind('/')+1); FILE* o = fopen((std::string(outdir)+"/"+cat+"__"+base+".f32").c_str(),"wb"); for(int i=0;i<total;++i){float m=0.5f*(L[i]+R[i]); fwrite(&m,4,1,o);} fclose(o);}
    // stats: RMS dB over 0.1-1.9 s (sustain), attack time to 50% of max 50ms-window RMS, release tail (time after off to -40 dB rel)
    auto rms = [&](int a0, int a1){ double s=0; for(int i=a0;i<a1;++i) s+=0.5*(L[i]*L[i]+R[i]*R[i]); return std::sqrt(s/std::max(1,a1-a0)); };
    double sus = rms((int)(0.1*sr),(int)(1.9*sr));
    int W=(int)(0.02*sr); double mx=0; std::vector<double> env; for(int i=0;i+W<=total;i+=W){double r=rms(i,i+W); env.push_back(r); mx=std::max(mx,r);} 
    double att=-1; for(size_t i=0;i<env.size();++i) if(env[i]>=0.5*mx){att=i*0.02-0.02; break;}
    double early = rms(on, on+(int)(0.25*sr)), late = rms((int)(1.6*sr),(int)(1.95*sr));
    double tail=0; for(size_t i=env.size(); i-->0;) if(env[i] > mx*0.01){ tail = i*0.02 - 2.0; break;}
    // spectral centroid-ish: zero crossing rate of the mid over sustain
    int zc=0; for(int i=(int)(0.3*sr)+1;i<(int)(1.9*sr);++i){ float m0=L[i-1]+R[i-1], m1=L[i]+R[i]; if((m0<0)!=(m1<0)) ++zc;} double zcr = zc/1.6/2.0;
    double peak=0; for(int i=0;i<total;++i) peak=std::max(peak,(double)std::max(std::fabs(L[i]),std::fabs(R[i])));
    auto db=[](double v){return v>1e-9?20*std::log10(v):-180.0;};
    std::string nm = path.substr(path.rfind('/',path.rfind('/')-1)+1);
    printf("%s\tsus_db=%.1f\tpeak_db=%.1f\tatt=%.2f\tdecay=%.1f\ttail=%.2f\tzcr=%.0f\n", nm.c_str(), db(sus), db(peak), att, db(late)-db(early), tail, zcr);
  }
}
