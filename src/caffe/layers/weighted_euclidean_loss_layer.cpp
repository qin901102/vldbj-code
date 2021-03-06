#include <vector>

#include "caffe/layer.hpp"
#include "caffe/util/io.hpp"
#include "caffe/util/math_functions.hpp"
#include "caffe/vision_layers.hpp"

namespace caffe {

template <typename Dtype>
void WeightedEuclideanLossLayer<Dtype>::Reshape(
  const vector<Blob<Dtype>*>& bottom, vector<Blob<Dtype>*>* top) {
  LossLayer<Dtype>::Reshape(bottom, top);
  CHECK_EQ(bottom[0]->channels(), bottom[1]->channels());
  CHECK_EQ(bottom[0]->height(), bottom[1]->height());
  CHECK_EQ(bottom[0]->width(), bottom[1]->width());
  diff_.Reshape(bottom[0]->num(), bottom[0]->channels(),
      bottom[0]->height(), bottom[0]->width());
  if(bottom.size()>2)
    CHECK_EQ(bottom[2]->count(),bottom[2]->num());
  if(bottom.size()>3)
    CHECK_EQ(bottom[3]->count(),bottom[3]->num());
}

template <typename Dtype>
void WeightedEuclideanLossLayer<Dtype>::Forward_cpu(const vector<Blob<Dtype>*>& bottom,
    vector<Blob<Dtype>*>* top) {
  int count = bottom[0]->count();
  caffe_sub(
      count,
      bottom[0]->cpu_data(),
      bottom[1]->cpu_data(),
      diff_.mutable_cpu_data());
  Dtype dot = caffe_cpu_dot(count, diff_.cpu_data(), diff_.cpu_data());
  Dtype loss = dot / bottom[0]->num() / Dtype(2);
  (*top)[0]->mutable_cpu_data()[0] = loss;
}

template <typename Dtype>
void WeightedEuclideanLossLayer<Dtype>::Backward_cpu(const vector<Blob<Dtype>*>& top,
    const vector<bool>& propagate_down, vector<Blob<Dtype>*>* bottom) {
  for (int i = 0; i < 2; ++i) {
    if (propagate_down[i]) {
      const Dtype sign = (i == 0) ? 1 : -1;
      const Dtype alpha = sign * top[0]->cpu_diff()[0] / (*bottom)[i]->num();
      if(bottom->size()>3){
        const Dtype* loss2=(*bottom)[2]->cpu_data();
        const Dtype* loss3=(*bottom)[3]->cpu_data();
        int num=(*bottom)[i]->num();
        int dim=(*bottom)[i]->count()/num;
        Dtype local_alpha=alpha;
        for(int j=0;j<num;j++){
          if(sign*(loss2[j]-loss3[j])<=0)
            local_alpha=Dtype(0);
          caffe_cpu_axpby(
              dim, 
              local_alpha,                       
              diff_.cpu_data()+j*dim,     
              Dtype(0),                  
              (*bottom)[i]->mutable_cpu_diff()+j*dim);
        }
      }else{
        caffe_cpu_axpby(
            (*bottom)[i]->count(),              // count
            alpha,                              // alpha
            diff_.cpu_data(),                   // a
            Dtype(0),                           // beta
            (*bottom)[i]->mutable_cpu_diff());  // b
      }
    }
  }
}

#ifdef CPU_ONLY
STUB_GPU(WeightedEuclideanLossLayer);
#endif

INSTANTIATE_CLASS(WeightedEuclideanLossLayer);

}  // namespace caffe
