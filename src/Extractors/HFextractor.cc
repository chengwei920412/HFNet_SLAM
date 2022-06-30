
#include <opencv2/core/core.hpp>
#include <opencv2/highgui/highgui.hpp>
#include <opencv2/features2d/features2d.hpp>
#include <opencv2/imgproc/imgproc.hpp>
#include <vector>
#include <iostream>

#include "Extractors/HFextractor.h"


using namespace cv;
using namespace std;

namespace ORB_SLAM3
{

const int PATCH_SIZE = 31;
const int HALF_PATCH_SIZE = 15;
const int EDGE_THRESHOLD = 19;

HFextractor::HFextractor(int _nfeatures, int _nNMSRadius, float _threshold,
                        float _scaleFactor, int _nlevels, const std::vector<BaseModel*>& _vpModels, bool _bUseOctTree):
        nfeatures(_nfeatures), nNMSRadius(_nNMSRadius), threshold(_threshold), mvpModels(_vpModels)
{
    scaleFactor = _scaleFactor;
    nlevels = _nlevels;
    bUseOctTree = _bUseOctTree;
    mvScaleFactor.resize(nlevels);
    mvLevelSigma2.resize(nlevels);
    mvScaleFactor[0]=1.0f;
    mvLevelSigma2[0]=1.0f;
    for(int i=1; i<nlevels; i++)
    {
        mvScaleFactor[i]=mvScaleFactor[i-1]*scaleFactor;
        mvLevelSigma2[i]=mvScaleFactor[i]*mvScaleFactor[i];
    }

    mvInvScaleFactor.resize(nlevels);
    mvInvLevelSigma2.resize(nlevels);
    for(int i=0; i<nlevels; i++)
    {
        mvInvScaleFactor[i]=1.0f/mvScaleFactor[i];
        mvInvLevelSigma2[i]=1.0f/mvLevelSigma2[i];
    }

    mvImagePyramid.resize(nlevels);

    mnFeaturesPerLevel.resize(nlevels);
    float factor = 1.0f / scaleFactor;
    float nDesiredFeaturesPerScale = nfeatures*(1 - factor)/(1 - (float)pow((double)factor, (double)nlevels));

    int sumFeatures = 0;
    for( int level = 0; level < nlevels-1; level++ )
    {
        mnFeaturesPerLevel[level] = cvRound(nDesiredFeaturesPerScale);
        sumFeatures += mnFeaturesPerLevel[level];
        nDesiredFeaturesPerScale *= factor;
    }
    mnFeaturesPerLevel[nlevels-1] = std::max(nfeatures - sumFeatures, 0);

    //This is for orientation
    // pre-compute the end of a row in a circular patch
    umax.resize(HALF_PATCH_SIZE + 1);

    int v, v0, vmax = cvFloor(HALF_PATCH_SIZE * sqrt(2.f) / 2 + 1);
    int vmin = cvCeil(HALF_PATCH_SIZE * sqrt(2.f) / 2);
    const double hp2 = HALF_PATCH_SIZE*HALF_PATCH_SIZE;
    for (v = 0; v <= vmax; ++v)
        umax[v] = cvRound(sqrt(hp2 - v * v));

    // Make sure we are symmetric
    for (v = HALF_PATCH_SIZE, v0 = 0; v >= vmin; --v)
    {
        while (umax[v0] == umax[v0 + 1])
            ++v0;
        umax[v] = v0;
        ++v0;
    }
}

void HFextractor::ComputePyramid(cv::Mat image)
{
    mvImagePyramid[0] = image;
    for (int level = 0; level < nlevels; ++level)
    {
        float scale = mvInvScaleFactor[level];
        Size sz(cvRound((float)image.cols*scale), cvRound((float)image.rows*scale));

        // Compute the resized image
        if( level != 0 )
        {
            resize(mvImagePyramid[level-1], mvImagePyramid[level], sz, 0, 0, INTER_LINEAR);
        }
    }
}


int HFextractor::operator() (const cv::Mat &_image, std::vector<cv::KeyPoint>& _keypoints,
                             cv::Mat &_localDescriptors, cv::Mat &_globalDescriptors)
{
    if(_image.empty()) return -1;
    assert(_image.type() == CV_8UC1 );
    _keypoints.clear();
    
    if (nlevels == 1)
    {
        mvpModels[0]->Detect(_image, _keypoints, _localDescriptors, _globalDescriptors, nfeatures, threshold, nNMSRadius);
    }
    else
    {
        ComputePyramid(_image);
        int nKeypoints = 0;
        vector<vector<cv::KeyPoint>> allKeypoints(nlevels);
        vector<cv::Mat> allDescriptors(nlevels);
        for (int level = 0; level < nlevels; ++level)
        {
            if (level == 0)
            {
                mvpModels[level]->Detect(mvImagePyramid[level], allKeypoints[level], allDescriptors[level], _globalDescriptors, mnFeaturesPerLevel[level], threshold, nNMSRadius);
            }
            else
            {
                mvpModels[level]->DetectOnlyLocal(mvImagePyramid[level], allKeypoints[level], allDescriptors[level], mnFeaturesPerLevel[level], threshold, ceil(nNMSRadius*mvInvScaleFactor[level]));
            }
            nKeypoints += allKeypoints[level].size();
        }
        _keypoints.reserve(nKeypoints);
        for (int level = 0; level < nlevels; ++level)
        {
            for (auto keypoint : allKeypoints[level])
            {
                keypoint.octave = level;
                keypoint.pt *= mvScaleFactor[level];
                _keypoints.emplace_back(keypoint);
            }
        }
        cv::vconcat(allDescriptors.data(), allDescriptors.size(), _localDescriptors);
    }
    return _keypoints.size();
}

} //namespace ORB_SLAM3