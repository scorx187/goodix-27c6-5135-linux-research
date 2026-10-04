// SIGFM algorithm for libfprint

// Copyright (C) 2022 Matthieu CHARETTE <matthieu.charette@gmail.com>
// Copyright (c) 2022 Natasha England-Elbro <ashenglandelbro@protonmail.com>
// Copyright (c) 2022 Timur Mangliev <tigrmango@gmail.com>

// This library is free software; you can redistribute it and/or
// modify it under the terms of the GNU Lesser General Public
// License as published by the Free Software Foundation; either
// version 2.1 of the License, or (at your option) any later version.

// This library is distributed in the hope that it will be useful,
// but WITHOUT ANY WARRANTY; without even the implied warranty of
// MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
// Lesser General Public License for more details.

// You should have received a copy of the GNU Lesser General Public
// License along with this library; if not, write to the Free Software
// Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA
//

#include "sigfm.hpp"
#include "binary.hpp"
#include "img-info.hpp"

#include "opencv2/core/persistence.hpp"
#include "opencv2/core/types.hpp"
#include "opencv2/features2d.hpp"
#include "opencv2/imgcodecs.hpp"
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <limits>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <sstream>
#include <string>

#include <opencv2/opencv.hpp>
#include <vector>

namespace bin {

template<>
struct serializer<SigfmImgInfo> : public std::true_type {
    static void serialize(const SigfmImgInfo& info, stream& out)
    {
        out << info.keypoints << info.descriptors;
    }
};

template<>
struct deserializer<SigfmImgInfo> : public std::true_type {
    static SigfmImgInfo deserialize(stream& in)
    {
        SigfmImgInfo info;
        in >> info.keypoints >> info.descriptors;
        return info;
    }
};
} // namespace bin

namespace {
constexpr auto distance_match = 0.75;
constexpr auto length_match = 0.05;
constexpr auto angle_match = 0.05;
constexpr auto min_match = 5;
struct match {
    cv::Point2i p1;
    cv::Point2i p2;
    match(cv::Point2i ip1, cv::Point2i ip2) : p1{ip1}, p2{ip2} {}
    match() : p1{cv::Point2i(0, 0)}, p2{cv::Point2i(0, 0)} {}
    bool operator==(const match& right) const
    {
        return std::tie(this->p1, this->p2) == std::tie(right.p1, right.p2);
    }
    bool operator<(const match& right) const
    {
        return std::tie(this->p1.y, this->p1.x) <
               std::tie(right.p1.y, right.p1.x);
    }
};
struct angle {
    double cos;
    double sin;
    match corr_matches[2];
    angle(double cos_, double sin_, match m1, match m2)
        : cos{cos_}, sin{sin_}, corr_matches{m1, m2}
    {
    }
};

constexpr double geometry_epsilon = 1e-12;

double relative_difference(double left, double right)
{
    if (!std::isfinite(left) || !std::isfinite(right)) {
        return std::numeric_limits<double>::infinity();
    }

    left = std::abs(left);
    right = std::abs(right);

    const double maximum = std::max(left, right);

    if (maximum <= geometry_epsilon) {
        return 0.0;
    }

    return std::abs(left - right) / maximum;
}

double clamp_unit(double value)
{
    return std::clamp(value, -1.0, 1.0);
}

bool geometry_angles(const int vec_1[2],
                     const int vec_2[2],
                     double* first_angle,
                     double* second_angle)
{
    if (first_angle == nullptr || second_angle == nullptr) {
        return false;
    }

    const double length_1 =
        std::hypot(
            static_cast<double>(vec_1[0]),
            static_cast<double>(vec_1[1]));

    const double length_2 =
        std::hypot(
            static_cast<double>(vec_2[0]),
            static_cast<double>(vec_2[1]));

    if (!std::isfinite(length_1) ||
        !std::isfinite(length_2) ||
        length_1 <= geometry_epsilon ||
        length_2 <= geometry_epsilon) {
        return false;
    }

    if (relative_difference(length_1, length_2) >
        length_match) {
        return false;
    }

    const double product =
        length_1 * length_2;

    if (!std::isfinite(product) ||
        product <= geometry_epsilon) {
        return false;
    }

    const double dot =
        static_cast<double>(vec_1[0]) * vec_2[0] +
        static_cast<double>(vec_1[1]) * vec_2[1];

    const double cross =
        static_cast<double>(vec_1[0]) * vec_2[1] -
        static_cast<double>(vec_1[1]) * vec_2[0];

    const double dot_ratio =
        clamp_unit(dot / product);

    const double cross_ratio =
        clamp_unit(cross / product);

    const double calculated_first =
        M_PI / 2 +
        std::asin(dot_ratio);

    const double calculated_second =
        std::acos(cross_ratio);

    if (!std::isfinite(calculated_first) ||
        !std::isfinite(calculated_second)) {
        return false;
    }

    *first_angle = calculated_first;
    *second_angle = calculated_second;

    return true;
}


bool sigfm_info_valid(const SigfmImgInfo* info)
{
    if (info == nullptr) {
        return false;
    }

    const std::size_t count =
        info->keypoints.size();

    if (count == 0 ||
        count > bin::max_container_elements) {
        return false;
    }

    const cv::Mat& descriptors =
        info->descriptors;

    if (descriptors.empty() ||
        descriptors.dims != 2 ||
        descriptors.type() != CV_32FC1 ||
        descriptors.cols !=
            bin::sigfm_descriptor_columns ||
        descriptors.rows !=
            static_cast<int>(count) ||
        !descriptors.isContinuous()) {
        return false;
    }

    const std::size_t descriptor_bytes =
        descriptors.total() *
        descriptors.elemSize();

    if (descriptor_bytes == 0 ||
        descriptor_bytes >
            bin::max_matrix_bytes) {
        return false;
    }

    for (const auto& keypoint :
         info->keypoints) {
        if (!std::isfinite(keypoint.pt.x) ||
            !std::isfinite(keypoint.pt.y) ||
            !std::isfinite(keypoint.angle) ||
            !std::isfinite(keypoint.response) ||
            !std::isfinite(keypoint.size) ||
            keypoint.size <= 0.0f) {
            return false;
        }
    }

    if (!cv::checkRange(descriptors)) {
        return false;
    }

    return true;
}

} // namespace

SigfmImgInfo* sigfm_copy_info(SigfmImgInfo* info)
{
    if (!sigfm_info_valid(info)) {
        return nullptr;
    }

    try {
        return new SigfmImgInfo{*info};
    }
    catch (...) {
        return nullptr;
    }
}


int sigfm_keypoints_count(SigfmImgInfo* info)
{
    if (info == nullptr ||
        info->keypoints.size() >
            static_cast<std::size_t>(
                std::numeric_limits<int>::max())) {
        return 0;
    }

    return static_cast<int>(
        info->keypoints.size());
}


unsigned char* sigfm_serialize_binary(SigfmImgInfo* info, int* outlen)
{
    if (outlen == nullptr) {
        return nullptr;
    }

    *outlen = 0;

    if (!sigfm_info_valid(info)) {
        return nullptr;
    }

    try {
        bin::stream s;
        s << *info;

        if (s.size() == 0 ||
            s.size() >
                bin::max_serialized_bytes ||
            s.size() >
                static_cast<std::size_t>(
                    std::numeric_limits<int>::max())) {
            return nullptr;
        }

        unsigned char* result =
            s.copy_buffer();

        if (result == nullptr) {
            return nullptr;
        }

        *outlen =
            static_cast<int>(s.size());

        return result;
    }
    catch (...) {
        return nullptr;
    }
}


SigfmImgInfo* sigfm_deserialize_binary(const unsigned char* bytes, int len)
{
    if (bytes == nullptr ||
        len <= 0 ||
        static_cast<std::size_t>(len) >
            bin::max_serialized_bytes) {
        return nullptr;
    }

    try {
        bin::stream s{
            bytes,
            bytes + len};

        auto info =
            std::make_unique<SigfmImgInfo>();

        s >> *info;

        if (s.size() != 0 ||
            !sigfm_info_valid(info.get())) {
            return nullptr;
        }

        return info.release();
    }
    catch (...) {
        return nullptr;
    }
}

SigfmImgInfo* sigfm_extract(const SigfmPix* pix, int width, int height)
{
    cv::Mat img;
    img.create(height, width, CV_8UC1);
    std::memcpy(img.data, pix, width * height);
    const auto roi = cv::Mat::ones(cv::Size{img.size[1], img.size[0]}, CV_8UC1);
    std::vector<cv::KeyPoint> pts;

    cv::Mat descs;
    cv::SIFT::create()->detectAndCompute(img, roi, pts, descs);

    auto* info = new SigfmImgInfo{pts, descs};
    return info;
}

int sigfm_match_score(SigfmImgInfo* frame, SigfmImgInfo* enrolled)
{
    if (!sigfm_info_valid(frame) ||
        !sigfm_info_valid(enrolled)) {
        return -1;
    }

    try {
        std::vector<std::vector<cv::DMatch>> points;
        auto bfm = cv::BFMatcher::create();
        bfm->knnMatch(frame->descriptors, enrolled->descriptors, points, 2);
        std::set<match> matches_unique;
        int nb_matched = 0;
        for (const auto& pts : points) {
            if (pts.size() < 2) {
                continue;
            }
            const cv::DMatch& match_1 = pts.at(0);
            if (match_1.distance < distance_match * pts.at(1).distance) {
                matches_unique.emplace(
                    match{frame->keypoints.at(match_1.queryIdx).pt,
                          enrolled->keypoints.at(match_1.trainIdx).pt});
                nb_matched++;
            }
        }
        if (nb_matched < min_match) {
            return 0;
        }
        std::vector<match> matches{matches_unique.begin(),
                                   matches_unique.end()};

        std::vector<angle> angles;
        for (std::size_t j = 0; j < matches.size(); j++) {
            match match_1 = matches[j];
            for (std::size_t k = j + 1; k < matches.size(); k++) {
                match match_2 = matches[k];

                int vec_1[2] = {match_1.p1.x - match_2.p1.x,
                                match_1.p1.y - match_2.p1.y};
                int vec_2[2] = {match_1.p2.x - match_2.p2.x,
                                match_1.p2.y - match_2.p2.y};

                double first_angle = 0.0;
                double second_angle = 0.0;

                if (geometry_angles(
                        vec_1,
                        vec_2,
                        &first_angle,
                        &second_angle)) {
                    angles.emplace_back(
                        angle(
                            first_angle,
                            second_angle,
                            match_1,
                            match_2));
                }
            }
        }

        if (angles.size() < min_match) {
            return 0;
        }

        int count = 0;
        for (std::size_t j = 0; j < angles.size(); j++) {
            angle angle_1 = angles[j];
            for (std::size_t k = j + 1; k < angles.size(); k++) {
                angle angle_2 = angles[k];

                if (relative_difference(
                        angle_1.sin,
                        angle_2.sin) <= angle_match &&
                    relative_difference(
                        angle_1.cos,
                        angle_2.cos) <= angle_match) {

                    count += 1;
                }
            }
        }
        return count;
    }
    catch (...) {
        return -1;
    }
}

void sigfm_free_info(SigfmImgInfo* info) { delete info; }
