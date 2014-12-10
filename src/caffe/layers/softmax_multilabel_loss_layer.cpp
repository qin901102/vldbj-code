#include <algorithm>
#include <cfloat>
#include <vector>

#include "caffe/layer.hpp"
#include "caffe/util/math_functions.hpp"
#include "caffe/vision_layers.hpp"

namespace caffe {

template <typename Dtype>
void SoftmaxMultiLabelLossLayer<Dtype>::LayerSetUp(
    const vector<Blob<Dtype>*>& bottom, vector<Blob<Dtype>*>* top) {
  LossLayer<Dtype>::LayerSetUp(bottom, top);
  softmax_bottom_vec_.clear();
  softmax_bottom_vec_.push_back(bottom[0]);
  softmax_top_vec_.clear();
  softmax_top_vec_.push_back(&prob_);
  softmax_layer_->SetUp(softmax_bottom_vec_, &softmax_top_vec_);
}

template <typename Dtype>
void SoftmaxMultiLabelLossLayer<Dtype>::Reshape(
    const vector<Blob<Dtype>*>& bottom, vector<Blob<Dtype>*>* top) {
  LossLayer<Dtype>::Reshape(bottom, top);
  softmax_layer_->Reshape(softmax_bottom_vec_, &softmax_top_vec_);
  if (top->size() >= 2) {
    // softmax output
    (*top)[1]->ReshapeLike(*bottom[0]);
  }
}

template <typename Dtype>
void SoftmaxMultiLabelLossLayer<Dtype>::Forward_cpu(
    const vector<Blob<Dtype>*>& bottom, vector<Blob<Dtype>*>* top) {
  // The forward pass computes the softmax prob values.
  softmax_layer_->Forward(softmax_bottom_vec_, &softmax_top_vec_);
  const Dtype* prob_data = prob_.cpu_data();
  const Dtype* label = bottom[1]->cpu_data();
  int num = prob_.num();
  int dim = prob_.count() / num;
  int spatial_dim = prob_.height() * prob_.width();
  int label_dim = bottom[1]->count()/num;
  CHECK_EQ(spatial_dim, 1);
  Dtype loss = 0;
  for (int i = 0; i < num; ++i) {
    Dtype tmp_loss=0;
    int num_labels=0;
    for( int j=0; label[i*label_dim+j]!=-1;++j){
      tmp_loss -= log(std::max(prob_data[i * dim +
          static_cast<int>(label[i * label_dim+j])], Dtype(FLT_MIN)));
      num_labels+=1;
    }
    loss+=tmp_loss/num_labels;
  }
  (*top)[0]->mutable_cpu_data()[0] = loss / num ;
  if (top->size() == 2) {
    (*top)[1]->ShareData(prob_);
  }
}

template <typename Dtype>
void SoftmaxMultiLabelLossLayer<Dtype>::Backward_cpu(const vector<Blob<Dtype>*>& top,
    const vector<bool>& propagate_down,
    vector<Blob<Dtype>*>* bottom) {
  if (propagate_down[1]) {
    LOG(FATAL) << this->type_name()
               << " Layer cannot backpropagate to label inputs.";
  }
  if (propagate_down[0]) {
    Dtype* bottom_diff = (*bottom)[0]->mutable_cpu_diff();
    const Dtype* prob_data = prob_.cpu_data();
    caffe_copy(prob_.count(), prob_data, bottom_diff);
    const Dtype* label = (*bottom)[1]->cpu_data();
    int num = prob_.num();
    int dim = prob_.count() / num;
    int spatial_dim = prob_.height() * prob_.width();
    int max_labels = (*bottom)[1]->count()/num;
    CHECK_EQ(spatial_dim, 1);
    for (int i = 0; i < num; ++i) {
      for( int j=0;label[i*max_labels+j]!=-1;++j)
        bottom_diff[i * dim + static_cast<int>(label[i * max_labels + j])] -= 1;
    }
    // Scale gradient
    const Dtype loss_weight = top[0]->cpu_diff()[0];
    caffe_scal(prob_.count(), loss_weight / num , bottom_diff);
  }
}


#ifdef CPU_ONLY
STUB_GPU(SoftmaxMultiLabelLossLayer);
#endif

INSTANTIATE_CLASS(SoftmaxMultiLabelLossLayer);


}  // namespace caffe
