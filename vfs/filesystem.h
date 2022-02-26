#pragma once

#include <filesystem>
#include <unordered_set>
#include <unordered_map>
#include <string>

struct fuse;

namespace vfs {
struct Abort : public std::exception {};

class File {
public:
	virtual std::size_t read(char *buf, std::size_t count, std::size_t pos) = 0;
	virtual std::size_t write(const char *buf, std::size_t count, std::size_t pos) = 0;
	virtual std::size_t size() = 0;
};

class Tree {
public:
	std::unordered_set<std::string> list(std::string prefix);
	std::shared_ptr<File> get(std::string filename);
	bool exists(std::string prefix);

	template <typename T>
	void put(std::string filename, T file) {
		m_files[filename] = std::make_shared<T>(file);
	}

private:
	std::unordered_map<std::string, std::shared_ptr<File>> m_files;
};

class Filesystem {
public:
	Filesystem(std::string mountPoint);
	~Filesystem();

	void shutdown();
	int run();

	Tree& tree() { return m_tree; }

private:
	struct fuse* m_fuse = NULL;
	Tree m_tree;
};
}
