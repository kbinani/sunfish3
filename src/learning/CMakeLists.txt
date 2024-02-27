cmake_minimum_required(VERSION 2.8)

project(sunfish CXX)

add_library(sunfish_learning STATIC
	Learning.cpp
	BatchLearning.cpp
	OnlineLearning.cpp
)
