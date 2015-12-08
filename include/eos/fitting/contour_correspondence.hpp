/*
 * Eos - A 3D Morphable Model fitting library written in modern C++11/14.
 *
 * File: include/eos/fitting/contour_correspondence.hpp
 *
 * Copyright 2015 Patrik Huber
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 * http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */
#pragma once

#ifndef CONTOURCORRESPONDENCE_HPP_
#define CONTOURCORRESPONDENCE_HPP_

#include "eos/core/Landmark.hpp"
#include "eos/morphablemodel/MorphableModel.hpp"

#include "cereal/archives/json.hpp"

#include "glm/gtc/matrix_transform.hpp"

#include "opencv2/core/core.hpp"

#ifdef WIN32
	#define BOOST_ALL_DYN_LINK	// Link against the dynamic boost lib. Seems to be necessary because we use /MD, i.e. link to the dynamic CRT.
	#define BOOST_ALL_NO_LIB	// Don't use the automatic library linking by boost with VS2010 (#pragma ...). Instead, we specify everything in cmake.
#endif
#include "boost/property_tree/ptree.hpp"
#include "boost/property_tree/info_parser.hpp"

#include <vector>
#include <string>
#include <algorithm>
#include <fstream>

namespace eos {
	namespace fitting {

// The contour (outline) on the right and left side of the reference face model.
// We should extend that to the 1724 model to get a few more points, it should
// improve the contour fitting.
struct ModelContour
{
	// starting from right side, eyebrow-height: (I think the order matters here)
	std::vector<int> right_contour;
	/* 23 = middle, below chin - not included in the contour here */
	// starting from left side, eyebrow-height:
	std::vector<int> left_contour;

	// We store r/l separately because we currently only fit to the contour facing the camera.
	// Also if we were to fit to the whole contour: Be careful not to just fit to the closest. The 
	// "invisible" ones behind might be closer on an e.g 90� angle. Store CNT for left/right side separately?
	
	/**
	 * Helper method to load a ModelContour from
	 * a json file from the harddisk.
	 *
	 * Eventually, it might be included in the MorphableModel class.
	 *
	 * @param[in] filename Filename to a model.
	 * @return The loaded ModelContour.
	 * @throw std::runtime_error When the file given in \c filename fails to be opened (most likely because the file doesn't exist).
	 */
	static ModelContour load(std::string filename)
	{
		ModelContour contour;

		std::ifstream file(filename, std::ios::binary);
		if (file.fail()) {
			throw std::runtime_error("Error opening given file: " + filename);
		}
		cereal::JSONInputArchive input_archive(file);
		input_archive(contour);

		return contour;
	};

	friend class cereal::access;
	/**
	 * Serialises this class using cereal.
	 *
	 * @param[in] ar The archive to serialise to (or to serialise from).
	 */
	template<class Archive>
	void serialize(Archive& archive)
	{
		archive(cereal::make_nvp("right_contour", right_contour), cereal::make_nvp("left_contour", left_contour));
	};
};

struct ContourLandmarks
{
	// starting from right side, eyebrow-height.
	std::vector<std::string> right_contour;
	// Chin point is not included in the contour here.
	// starting from left side, eyebrow-height. Order doesn't matter here.
	std::vector<std::string> left_contour;

	// We store r/l separately because we currently only fit to the contour facing the camera.
	
	/**
	 * Helper method to load contour landmarks from a text file with landmark
	 * mappings, like ibug2did.txt.
	 *
	 * @param[in] filename Filename to a landmark-mapping file.
	 * @return A ContourLandmarks instance with loaded 2D contour landmarks.
	 * @throw std::runtime_error When the file given in \c filename fails to be opened (most likely because the file doesn't exist).
	 */
	static ContourLandmarks load(std::string filename)
	{
		ContourLandmarks contour;

		using std::string;
		using boost::property_tree::ptree;
		ptree configtree;
		try {
			boost::property_tree::info_parser::read_info(filename, configtree);
		}
		catch (const boost::property_tree::ptree_error& error) {
			throw std::runtime_error(string("ContourLandmarks: Error reading landmark-mappings file: ") + error.what());
		}

		try {
			// Todo: I think we should improve error handling here. When there's no contour_landmarks in the file, it
			// should still work, the returned vectors should just be empty.
			ptree right_contour = configtree.get_child("contour_landmarks.right");
			for (auto&& landmark : right_contour) {
				contour.right_contour.emplace_back(landmark.first);
			}
			ptree left_contour = configtree.get_child("contour_landmarks.left");
			for (auto&& landmark : left_contour) {
				contour.left_contour.emplace_back(landmark.first);
			}
		}
		catch (const boost::property_tree::ptree_error& error) {
			throw std::runtime_error(string("ContourLandmarks: Error while parsing the mappings file: ") + error.what());
		}
		catch (const std::runtime_error& error) {
			throw std::runtime_error(string("ContourLandmarks: Error while parsing the mappings file: ") + error.what());
		}

		return contour;
	};
};

/**
 * Takes a ... returns two vectors... can have different size. Does not establish correspondence. Use get_nearest_contour_points() for that.
 *
 * Note: Maybe rename to find_nearest_contour_points, to highlight that there is (potentially a lot) computational cost involved?
 *
 * @param[in] yaw_angle yaw angle in degrees.
 * @param[in] contour_landmarks E.g. ibug contours.
 * @param[in] model_contour X.
 * @return A tuple/two vectors... with X. returns ... model_cnt_idx different size than 2d_cnt. Not in correspondence.
 */
std::pair<std::vector<std::string>, std::vector<int>> select_contour(float yaw_angle, const ContourLandmarks& contour_landmarks, const ModelContour& model_contour)
{
	std::vector<int> model_contour_indices;
	std::vector<std::string> contour_landmark_identifiers;
	if (yaw_angle >= 0.0f) { // positive yaw = subject looking to the left
		model_contour_indices = model_contour.right_contour; // ==> we use the right cnt-lms
		contour_landmark_identifiers = contour_landmarks.right_contour;
	}
	else {
		model_contour_indices = model_contour.left_contour;
		contour_landmark_identifiers = contour_landmarks.left_contour;
	}
	return std::make_pair(contour_landmark_identifiers, model_contour_indices);
};

/**
 * Given a set of 2D image landmarks, finds the closest (in a L2 sense) 3D vertex from a list of vertices.
 *
 * Note: Maybe rename to find_nearest_contour_points, to highlight that there is (potentially a lot) computational cost involved?
 *
 * @param[in] landmarks All image landmarks.
 * @param[in] landmark_contour_identifiers ibug contour ids of left or right side.
 * @param[in] model_contour_indices The model contour indices that should be used/considered to find the closest corresponding 3D vertex.
 * @param[in] morphable_model The Morphable Model whose shape (coefficients) are estimated.
 * @param[in] view_model x.
 * @param[in] ortho_projection Note: Does this depend on ortho? Maybe not? If it works with persp too, then rename param & doc.
 * @param[in] viewport X.
 * @return A tuple with the 2D contour landmark points, the corresponding points in the 3D shape model and their vertex indices.
 */
// actually, only return the vertex id, not the point? Same with get_corresponding_pointset? Because
// then it's much easier to use the current shape estimate instead of the mean! But this function needs to project.
// So... it should take a Mesh actually? But creating a Mesh is a lot of computation?
// When we want to use the non-mean, then we need to use draw_sample() anyway? So overhead of Mesh is only if we use the mean?
// Maybe two overloads?
// Note: Uses the mean to calculate.
std::tuple<std::vector<cv::Vec2f>, std::vector<cv::Vec4f>, std::vector<int>> get_nearest_contour_correspondences(const eos::core::LandmarkCollection<cv::Vec2f>& landmarks, const std::vector<std::string>& landmark_contour_identifiers, const std::vector<int>& model_contour_indices, const morphablemodel::MorphableModel& morphable_model, const glm::mat4x4& view_model, const glm::mat4x4& ortho_projection, const glm::vec4& viewport)
{
	// These are the additional contour-correspondences we're going to find and then use!
	std::vector<cv::Vec4f> model_points_cnt; // the points in the 3D shape model
	std::vector<int> vertex_indices_cnt; // their vertex indices
	std::vector<cv::Vec2f> image_points_cnt; // the corresponding 2D landmark points

	// For each 2D-CNT-LM, find the closest 3DMM-CNT-LM and add to correspondences:
	// Note: If we were to do this for all 3DMM vertices, then ray-casting (i.e. glm::unproject) would be quicker to find the closest vertex)
	for (auto&& ibug_idx : landmark_contour_identifiers)
	{
		// Check if the contour landmark is amongst the landmarks given to us (from detector or ground truth):
		// (Note: Alternatively, we could filter landmarks beforehand and then just loop over landmarks => means one less function param here. Separate filtering from actual algorithm.)
		auto result = std::find_if(begin(landmarks), end(landmarks), [&ibug_idx](auto&& e) { return e.name == ibug_idx; }); // => this can go outside the loop
		// TODO Check for ::end!!! if it's not found!
		cv::Vec2f screen_point_2d_contour_landmark = result->coordinates;

		std::vector<float> distances_2d;
		for (auto&& model_contour_vertex_idx : model_contour_indices) // we could actually pre-project them, i.e. only project them once, not for each landmark newly...
		{
			auto vertex = morphable_model.get_shape_model().get_mean_at_point(model_contour_vertex_idx);
			glm::vec3 proj = glm::project(glm::vec3{ vertex[0], vertex[1], vertex[2] }, view_model, ortho_projection, viewport);
			cv::Vec2f screen_point_model_contour(proj.x, proj.y);

			double dist = cv::norm(screen_point_model_contour, screen_point_2d_contour_landmark, cv::NORM_L2);
			distances_2d.emplace_back(dist);
		}
		auto min_ele = std::min_element(begin(distances_2d), end(distances_2d));
		// Todo: Cover the case when cnt_indices_to_use.size() is 0.
		auto min_ele_idx = std::distance(begin(distances_2d), min_ele);
		auto the_3dmm_vertex_id_that_is_closest = model_contour_indices[min_ele_idx];

		cv::Vec4f vertex = morphable_model.get_shape_model().get_mean_at_point(the_3dmm_vertex_id_that_is_closest);
		model_points_cnt.emplace_back(vertex);
		vertex_indices_cnt.emplace_back(the_3dmm_vertex_id_that_is_closest);
		image_points_cnt.emplace_back(screen_point_2d_contour_landmark);
	}

	return std::make_tuple(image_points_cnt, model_points_cnt, vertex_indices_cnt);
};

	} /* namespace fitting */
} /* namespace eos */

#endif /* CONTOURCORRESPONDENCE_HPP_ */
