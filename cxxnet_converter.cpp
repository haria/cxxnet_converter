/*!
 * \file cxx_convert.cpp v:1.2.2.3
 * \brief convert cxx model to caffe model
 * \brief generations of automatic caffe prototxt would be released in future version
 * \author Mrinal Haloi 
 */


#define _CRT_SECURE_NO_WARNINGS
#define _CRT_SECURE_NO_DEPRECATE

#include <ctime>
#include <string>
#include <cstring>
#include <vector>
#include <nnet/nnet.h>
#include <utils/config.h>
#include <nnet/neural_net-inl.hpp>
#include <nnet/nnet_impl-inl.hpp>
#include <layer/convolution_layer-inl.hpp>
#include <layer/cudnn_convolution_layer-inl.hpp>
#include <layer/fullc_layer-inl.hpp>

#include "caffe/data_layers.hpp"
#include "caffe/layer.hpp"
#include <caffe/blob.hpp>
#include <caffe/solver.hpp>
#include <caffe/net.hpp>
#include <caffe/util/io.hpp>
#include <caffe/vision_layers.hpp>
#include <caffe/proto/caffe.pb.h>
#include <caffe/util/hdf5.hpp>
#include <caffe/util/io.hpp>
#include <caffe/util/math_functions.hpp>
#include <caffe/util/upgrade_proto.hpp>

using namespace std;
namespace cxxnet {
	
	class CxxConverter {
  	public:
	/*! \brief constructor to set type of cxxnet */
    	CxxConverter() {
      		this->net_type_ = 0;
    	}
	/*! \brief destructors just free up unused space */
    	~CxxConverter() {
    		if (net_trainer_ != NULL) {
      			delete net_trainer_;
    		}
    	}	
	/*! \brief top level function to convert cxxnet model to caffe model */
    	void Convert(int argc, char *argv[]) {
      		if (argc != 5) {
        		printf("Usage: <cxx_config_path> <cxx_model_path> <caffe_proto_path> <caffe_model_output_path>\n");
        		return;
      		}
		
		cxxnet::utils::StdFile fcc(argv[3], "r");
		cxxnet::utils::StdFile fmc(argv[4], "wb");

      		this->InitCaffe(argv[3]);
		this->InitCXX(argv[1], argv[2]);
      		this->TransferNet();
      		this->SaveModel(argv[4], argv[3]);
    	}
	private:
	/*! \brief intilaize cxxnet model with layers paramters and model weight/bias vector */
	inline void InitCXX(const char *cxx_config, const char* cxx_model_file){
		utils::ConfigIterator itr(cxx_config);
		dmlc::Stream *fm = dmlc::Stream::Create(cxx_model_file, "r");
		CHECK(fm->Read(&net_type_, sizeof(int)) != 0) << " invalid model file";
		while(itr.Next()){
			this->SetParam(itr.name(), itr.val());
		}
		net_trainer_ = this->CreateNet();
		net_trainer_->LoadModel(*fm);
	}


	/*! \brief Intialize caffe model with prototype/layers parameters */
    	inline void InitCaffe(const char *caffe_proto) {
      		caffe::Caffe::set_mode(caffe::Caffe::CPU);

      		caffe_net_.reset(new caffe::Net<float>(caffe_proto, caffe::TEST));
   	}
	/*! \brief function to transfer weight/bias from cxxnet to caffe net */
	inline void TransferNet(){
		const vector<caffe::shared_ptr<caffe::Layer<float> > >& caffe_layers = caffe_net_->layers();
      		const vector<string> & layer_names = caffe_net_->layer_names();
		
		
		for (size_t i = 0; i < layer_names.size(); ++i) {
			std::cout << layer_names.size() <<std::endl;
        		if (caffe::InnerProductLayer<float> *caffe_layer = dynamic_cast<caffe::InnerProductLayer<float> *>(caffe_layers[i].get())) {
          			printf("Dumping InnerProductLayer %s\n", layer_names[i].c_str());
				

				/*! \brief get layers parameter as blob data structure, where first channel of blob would caryy layers weight and second channel carry their respective bias  */ 
                                vector<caffe::shared_ptr<caffe::Blob<float> > >& blobs = caffe_layer->blobs();
                                caffe::Blob<float> &caffe_weight = *blobs[0];
                                caffe::Blob<float> &caffe_bias = *blobs[1];
				
				/*! \brief Get outweight & bias vectors and their corresponding shape from cxxnet model */
				mshadow::TensorContainer<cpu, 2, float> outweight;
                                mshadow::TensorContainer<cpu, 2, float> outbias;
                                std::vector<index_t> shape_weight;
                                std::vector<index_t> shape_bias;
                                net_trainer_->GetWeight(&outweight, &shape_weight, layer_names[i].c_str(), "wmat");
                                net_trainer_->GetWeight(&outbias, &shape_bias, layer_names[i].c_str(),"bias");

				/*! \brief transfer the data from cxxnet to caffe net using their respective data storage pattern */
				float* dataWeight = new float[caffe_weight.count()];
				for(index_t r = 0; r < outweight.size(0); ++r){
					mshadow::Tensor<mshadow::cpu, 2> weightFlat = outweight[r].FlatTo2D();
					for(index_t c = 0; c < weightFlat.size(1); ++c){
						float weight_data = weightFlat[0].dptr_[c];
						dataWeight[r*outweight.size(0) + c] = weight_data;
					}
				}
				caffe::caffe_copy(caffe_weight.count(), dataWeight, caffe_weight.mutable_cpu_data());
				/*! \brief transfer bias value from cxxnet to caffe net */
				float* dataBias = new float(caffe_bias.count());
				for(index_t b = 0; b < outbias.size(0); ++b){
					mshadow::Tensor<mshadow::cpu, 2> biasFlat = outbias[b].FlatTo2D();
					float bias_data = biasFlat[0].dptr_[0];
					dataBias[b] = bias_data;
				}
				caffe::caffe_copy(caffe_bias.count(), dataBias, caffe_bias.mutable_cpu_data());
			}else if (caffe::ConvolutionLayer<float> *caffe_layer = dynamic_cast<caffe::ConvolutionLayer<float> *>(caffe_layers[i].get())) {
          			printf("Dumping ConvolutionLayer %s\n", layer_names[i].c_str());
				

				/*! \brief get layers parameter as blob data structure, where first channel of blob would caryy layers weight and second channel carry their respective bias */
                                vector<caffe::shared_ptr<caffe::Blob<float> > >& blobs = caffe_layer->blobs();
                                caffe::Blob<float> &caffe_weight = *blobs[0]; 
                                caffe::Blob<float> &caffe_bias = *blobs[1];

				/*! \brief Weight & Get bias vectors from a single layer and it's corresponding size */
				mshadow::TensorContainer<cpu, 2, float> outweight;
				mshadow::TensorContainer<cpu, 2, float> outbias;
				std::vector<index_t> shape_weight;
				std::vector<index_t> shape_bias;
				net_trainer_->GetWeight(&outweight, &shape_weight, layer_names[i].c_str(), "wmat");
				net_trainer_->GetWeight(&outbias, &shape_bias, layer_names[i].c_str(),"bias");
				//std::cout << outweight.size(0)<< std::endl;
				
				/*! \brief transfer the data from cxxnet to caffe net using their respective data storage pattern  */
				float* dataWeight = new float[caffe_weight.count()];
				int idx = 0;	
          			for (int r = 0; r < caffe_weight.num(); ++r) {
					mshadow::Tensor<mshadow::cpu, 2> dataFlat = outweight[r].FlatTo2D();
            				for (int c = 0; c < caffe_weight.channels(); ++c) {
              					for (int h = 0; h < caffe_weight.height(); ++h) {
                					for (int w = 0; w < caffe_weight.width(); ++w) {
								float weight_data = dataFlat[0].dptr_[(c * caffe_weight.height() + h) * caffe_weight.width() + w];
								dataWeight[idx] = weight_data;
								idx++;

                					} 
              					} 
            				} 
          			} 
				caffe::caffe_copy(caffe_weight.count(), dataWeight, caffe_weight.mutable_cpu_data());
				/*! \brief transfer bias value from cxxnet to caffe net */
				float* dataBias = new float[caffe_bias.count()];
          			for (index_t b = 0; b < outbias.size(0); ++b) {
					mshadow::Tensor<mshadow::cpu, 2> biasFlat = outbias[b].FlatTo2D();
					float bias_data = biasFlat[0].dptr_[0];
					dataBias[b] = bias_data;
          			}
				caffe::caffe_copy(caffe_bias.count(), dataBias, caffe_bias.mutable_cpu_data());
			}else {
          			printf("Ignoring layer %s\n", layer_names[i].c_str());
        		}
		}
	
	}

   	



	/*! \brief Save caffe net model to disk */
	inline void SaveModel(const char* caffe_model_path, const char* caffe_solver_proto){
		caffe::NetParameter net_param;
  		caffe_net_->ToProto(&net_param, false);//, param.snapshot_diff());
  		caffe::WriteProtoToBinaryFile(net_param, caffe_model_path);
	}



	/*! \brief Set paramters of cxxnet net structures  */
	inline void SetParam(const char *name , const char *val) {
      		cfg_.push_back(std::make_pair(std::string(name), std::string(val)));
    	}

    	/*! \brief create a neural net using cxxnet module to load cxxnet model and assign parameter using cxxnet model config  */
    	inline nnet::INetTrainer* CreateNet(void) {
      		nnet::INetTrainer *net = nnet::CreateNet<mshadow::cpu>(net_type_);

      		for (size_t i = 0; i < cfg_.size(); ++ i) {
        		net->SetParam(cfg_[i].first.c_str(), cfg_[i].second.c_str());
      		}
      		return net;
    	}


  	private:
    	/*! \brief type of net implementation */
   	int net_type_;
    	/*! \brief trainer */
	nnet::INetTrainer *net_trainer_;
  	private:
    	/*! \brief all the configurations */
    	std::vector<std::pair<std::string, std::string> > cfg_;
    	/*! \brief caffe net reference */
    	caffe::shared_ptr<caffe::Net<float> > caffe_net_;
};
}


/*! \brief main function to use from API to convert model from cxxnet model to caffe model */
 int main(int argc, char *argv[]) {
	/*! \brief define a instance of CxxConverter*/
  	cxxnet::CxxConverter converter;
	/*! \brief call function convert to convert the specified model using definite configuration file from caffe and cxxnet*/
  	converter.Convert(argc, argv);
  	return 0;
}
