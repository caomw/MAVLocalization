//
//  SCRF_tree.cpp
//  LoopClosure
//
//  Created by jimmy on 2016-03-19.
//  Copyright © 2016 jimmy. All rights reserved.
//

#include "SCRF_tree.hpp"
#include <algorithm>
#include <iostream>
#include <limits>


using cv::Vec2i;
using namespace std;

bool SCRF_tree::build(const vector<SCRF_learning_sample> & samples,
                      const vector<cv::Mat> & rgbImages,                      
                      const SCRF_tree_parameter & param)
{
    root_ = new SCRF_tree_node();
    root_->depth_ = 0;
    
    vector<unsigned int> indices;
    for (int i = 0; i<samples.size(); i++) {
        indices.push_back(i);
    }
    std::random_shuffle(indices.begin(), indices.end());
    
    // set random number
    
    rng_ = cv::RNG(std::time(0));
    return this->configure_node(samples, rgbImages, indices, 0, root_, param);
}

bool SCRF_tree::predict(SCRF_testing_sample & testing_sample,
                        const cv::Mat & rgbImage) const
{
    assert(root_);
    return this->predict(root_, testing_sample, rgbImage);
}

bool SCRF_tree::predict(const SCRF_tree_node * const node,
                        SCRF_testing_sample & sample,
                        const cv::Mat & rgbImage) const
{
    if (node->is_leaf_) {
        sample.predict_p3d_ = node->p3d_;
        return true;
    }
    else
    {        
        cv::Vec2i p1 = sample.get_displacement(node->split_param_.d1_);
        cv::Vec2i p2 = sample.get_displacement(node->split_param_.d2_);
        
        bool is_inside_image1 = SCRF_Util::is_inside_image(rgbImage.cols, rgbImage.rows, p1[0], p1[1]);
        bool is_inside_image2 = SCRF_Util::is_inside_image(rgbImage.cols, rgbImage.rows, p2[0], p2[1]);
        if (is_inside_image1 && is_inside_image2) {
            cv::Vec3b pixel_1 = rgbImage.at<cv::Vec3b>(p1[1], p1[0]);
            cv::Vec3b pixel_2 = rgbImage.at<cv::Vec3b>(p2[1], p2[0]);
            
            double pixel_1_c = pixel_1[node->split_param_.c1_];
            double pixel_2_c = pixel_2[node->split_param_.c2_];
            double pixel_dif = pixel_1_c - pixel_2_c;
            if (pixel_dif < node->split_param_.threhold_ && node->left_node_) {
                return this->predict(node->left_node_, sample, rgbImage);
            }
            else if(node->right_node_)
            {
                return this->predict(node->right_node_, sample, rgbImage);
            }
        }
        else
        {
            return false;
        }
    }
    return true;
}

static vector<double> random_number_from_range(double min_val, double max_val, int rnd_num)

{
    assert(rnd_num > 0);
    
    cv::RNG rng;
    vector<double> data;
    for (int i = 0; i<rnd_num; i++) {
        data.push_back(rng.uniform(min_val, max_val));
    }
    return data;
}


static double best_split_random_parameter(const vector<SCRF_learning_sample> & samples,
                                          const vector<cv::Mat> & rgbImages,
                                          const vector<unsigned int> & indices,
                                          SCRF_split_parameter & split_param,
                                          int min_node_size,
                                          vector<unsigned int> & left_indices,
                                          vector<unsigned int> & right_indices)
{
    double min_loss = std::numeric_limits<double>::max();
    
    // calculate pixel difference
    int valid_num = 0;
    vector<double> pixel_difs(indices.size(), 0.0); // 0.0 for invalid pixels, @todo
    
    for (int i = 0; i<indices.size(); i++) {
        int index = indices[i];
        assert(index >= 0 && index < samples.size());
        SCRF_learning_sample example = samples[index];
        cv::Vec2i p1 = example.get_displacement(split_param.d1_);
        cv::Vec2i p2 = example.get_displacement(split_param.d2_);
        
        
        const cv::Mat rgb_image = rgbImages[example.image_index_];
        
        bool is_inside_image1 = SCRF_Util::is_inside_image(rgb_image.cols, rgb_image.rows, p1[0], p1[1]);
        bool is_inside_image2 = SCRF_Util::is_inside_image(rgb_image.cols, rgb_image.rows, p2[0], p2[1]);
        if (!is_inside_image1 || !is_inside_image2) {
            // at least one of sampled place is out side of the image  @todo
            pixel_difs[i] = 0.0;
            continue;
        }
        valid_num++;
        
        cv::Vec3b pixel_1 = rgb_image.at<cv::Vec3b>(p1[1], p1[0]);
        cv::Vec3b pixel_2 = rgb_image.at<cv::Vec3b>(p2[1], p2[0]);
        
        double pixel_1_c = pixel_1[split_param.c1_];
        double pixel_2_c = pixel_2[split_param.c2_];
        pixel_difs[i] = pixel_1_c - pixel_2_c;
    }
    double valid_ratio = 1.0*valid_num/pixel_difs.size();
//    printf("valid number is %d in %d, percentage %f\n", valid_num, (int)pixel_difs.size(), valid_ratio);
    if (valid_ratio < 0.8) {
    //    printf("valid ratio failed\n");
        return min_loss;
    }
    
    double min_v = *std::min_element(pixel_difs.begin(), pixel_difs.end());
    double max_v = *std::max_element(pixel_difs.begin(), pixel_difs.end());
    
    vector<double> split_values = random_number_from_range(min_v, max_v, 20);  // @todo
   // printf("number of randomly selected spliting values is %lu\n", split_values.size());
    
    // split data by pixel difference
    bool is_split = false;
    for (int i = 0; i<split_values.size(); i++) {
        double split_v = split_values[i];
        vector<unsigned int> cur_left_index;
        vector<unsigned int> cur_right_index;
        double cur_loss = 0;
        for (int j = 0; j<pixel_difs.size(); j++) {
            int index = indices[j];
            if (pixel_difs[j] < split_v) {
                cur_left_index.push_back(index);
            }
            else
            {
                cur_right_index.push_back(index);
            }
        }
        if (cur_left_index.size() < 2 * min_node_size || cur_right_index.size() < 2 * min_node_size) {
            continue;
        }
        cur_loss = SCRF_Util::spatial_variance(samples, cur_left_index);
        if (cur_loss > min_loss) {
            continue;
        }
        cur_loss += SCRF_Util::spatial_variance(samples, cur_right_index);
        if (cur_loss < min_loss) {
            is_split = true;
            min_loss = cur_loss;
            left_indices  = cur_left_index;
            right_indices = cur_right_index;
            split_param.threhold_ = split_v;
            //printf("split value is %lf\n", split_param.threhold_);
        }
    }
    if (!is_split) {
        return min_loss;
    }
    assert(left_indices.size() + right_indices.size() == indices.size());
    
    return min_loss;
}

bool SCRF_tree::configure_node(const vector<SCRF_learning_sample> & samples,
                               const vector<cv::Mat> & rgbImages,
                               const vector<unsigned int> & indices,
                               int depth,
                               SCRF_tree_node *node,
                               const SCRF_tree_parameter & param)
                               
{
    //assert(indices.size() <= rgbImages.size());
    
    if (depth >= param.max_depth_ || indices.size() < param.min_leaf_node_) {
        node->depth_ = depth;
        node->is_leaf_ = true;
        node->p3d_ = SCRF_Util::mean_location(samples, indices);
        double variance = SCRF_Util::spatial_variance(samples, indices);
        printf("depth, num_leaf_node, variance are %d, %lu, %lf\n", depth, indices.size(), variance);
        cout<<"mean location is "<<node->p3d_<<endl;
        return true;
    }    
    
    const int max_pixel_offset = 131;
    const int max_channel = 3;
    const int max_random_num = 100;
    const int min_node_size = param.min_leaf_node_;
    double min_loss = std::numeric_limits<double>::max();
    vector<unsigned int> left_indices;
    vector<unsigned int> right_indices;
    SCRF_split_parameter split_param;
    bool is_split = false;
    for (int i = 0; i<max_random_num; i++) {
        double x1 = rng_.uniform(-max_pixel_offset, max_pixel_offset);
        double y1 = rng_.uniform(-max_pixel_offset, max_pixel_offset);
        double x2 = rng_.uniform(-max_pixel_offset, max_pixel_offset);
        double y2 = rng_.uniform(-max_pixel_offset, max_pixel_offset);
        int c1 = rand()%max_channel;
        int c2 = rand()%max_channel;
        
        SCRF_split_parameter cur_split_param;
        cur_split_param.d1_ = cv::Vec2d(x1, y1);
        cur_split_param.d2_ = cv::Vec2d(x2, y2);
        cur_split_param.c1_ = c1;
        cur_split_param.c2_ = c2;
        
        vector<unsigned int> cur_left_indices;
        vector<unsigned int> cur_right_indices;
        
        double cur_loss = best_split_random_parameter(samples, rgbImages, indices, cur_split_param, min_node_size, cur_left_indices, cur_right_indices);
        if (cur_loss < min_loss) {
            is_split = true;
            min_loss = cur_loss;
            left_indices  = cur_left_indices;
            right_indices = cur_right_indices;
            split_param = cur_split_param;
        }
    }
    
    if (is_split) {
        assert(left_indices.size() + right_indices.size() == indices.size());
        printf("left, right node number is %lu %lu, percentage: %f loss: %lf\n", left_indices.size(), right_indices.size(), 1.0*left_indices.size()/indices.size(), min_loss);

        node->split_param_ = split_param;
    //    cout<<"split parameter is "<<split_param.d1_<<" "<<split_param.d2_<<endl;
        SCRF_tree_node *left_node = new SCRF_tree_node();
        this->configure_node(samples, rgbImages, left_indices, depth + 1, left_node, param);
        node->left_node_ = left_node;
        
        SCRF_tree_node *right_node = new SCRF_tree_node();
        this->configure_node(samples, rgbImages, right_indices, depth + 1, right_node, param);
        node->right_node_ = right_node;
        return true;
    }
    else
    {
     //   printf("split failed!\n");
        node->depth_   = depth;
        node->is_leaf_ = true;
        node->p3d_ = SCRF_Util::mean_location(samples, indices);
        double variance = SCRF_Util::spatial_variance(samples, indices);
        printf("depth, num_leaf_node, variance are %d, %lu, %lf\n", depth, indices.size(), variance);
        cout<<"mean location is "<<node->p3d_<<endl<<endl;
        return true;
    }
}







