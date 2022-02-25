#define FUSE_USE_VERSION 35

#include <iostream>
#include <fuse.h>

#include "filesystem.h"

static int dsp56720_open(const char *path, struct fuse_file_info *fi) {
	vfs::Tree* tree = reinterpret_cast<vfs::Tree*>(fuse_get_context()->private_data);

	std::cerr << "open: " << path << std::endl;
	auto file = tree->get(path);
	if (!file) {
		return -ENOENT;
	}

	return 0;
}

static int dsp56720_read(const char *path, char *buf, size_t size, off_t offset, struct fuse_file_info *fi) {
	vfs::Tree* tree = reinterpret_cast<vfs::Tree*>(fuse_get_context()->private_data);

	std::cerr << "read: " << path << std::endl;
	auto file = tree->get(path);
	if (!file) {
		return -ENOENT;
	}


	return file->read(buf, size, offset);
};

static int dsp56720_write(const char *path, const char *buf, size_t size,
		      off_t offset, struct fuse_file_info *fi)
{
	vfs::Tree* tree = reinterpret_cast<vfs::Tree*>(fuse_get_context()->private_data);

	std::cerr << "write: " << path << std::endl;
	auto file = tree->get(path);
	if (!file) {
		return -ENOENT;
	}

	return file->write(buf, size, offset);
}

static int dsp56720_readdir(const char *path, void *buf, fuse_fill_dir_t filler,
			off_t offset, struct fuse_file_info *fi,
			enum fuse_readdir_flags flags)
{
	vfs::Tree* tree = reinterpret_cast<vfs::Tree*>(fuse_get_context()->private_data);

	std::cerr << "readdir: " << path << std::endl;
	if (!tree->exists(path)) {
		return -ENOENT;
	}

	filler(buf, ".", NULL, 0, fuse_fill_dir_flags(0));
	filler(buf, "..", NULL, 0, fuse_fill_dir_flags(0));

	for (auto const& filename : tree->list(path)) {
		filler(buf, filename.c_str(), NULL, 0, fuse_fill_dir_flags(0));
	}

	return 0;
}

static int dsp56720_getattr(const char *path, struct stat *stbuf, struct fuse_file_info *fi) {
	vfs::Tree* tree = reinterpret_cast<vfs::Tree*>(fuse_get_context()->private_data);

	std::cerr << "getattr: " << path << std::endl;

	if (tree->exists(path)) {
		auto file = tree->get(path);

		if (file) {
			stbuf->st_mode = 0755 | S_IFREG;
			stbuf->st_nlink = 1;
			stbuf->st_size = file->size();
		} else {
			stbuf->st_mode = 0755 | S_IFDIR;
			stbuf->st_nlink = 2;
		}

		return 0;
	}

	return -ENOENT;
}

static const struct fuse_operations operations = {
	.getattr = dsp56720_getattr,
	.open    = dsp56720_open,
	.read	 = dsp56720_read,
	.write	 = dsp56720_write,
	.readdir = dsp56720_readdir,
};

std::unordered_set<std::string> vfs::Tree::list(std::string prefix) {
	// Prefix must have a trailing slash
	if (prefix.back() != '/') {
		prefix += '/';
	}

	std::unordered_set<std::string> unique;
	for (auto const& pair : m_files) {
		auto const& path = pair.first;

		if (path.rfind(prefix, 0) != 0) {
			continue;
		}

		auto start = prefix.size();
		auto end = path.find('/', start);

		unique.emplace(path.substr(start, end-start));
	}

	return unique;
}

std::shared_ptr<vfs::File> vfs::Tree::get(std::string filename) {
	auto it = m_files.find(filename);
	if (it == m_files.end()) {
		return nullptr;
	}

	return it->second;
}

bool vfs::Tree::exists(std::string prefix) {
	for (auto const& pair : m_files) {
		auto const& path = pair.first;

		if (path.rfind(prefix, 0) == 0) {
			return true;
		}
	}

	return false;
}

vfs::Filesystem::Filesystem(std::string mountPoint) : m_fuse(nullptr) {
	char arg[] = "dsp";
	char *argv[] = { reinterpret_cast<char*>(&arg) };

	struct fuse_args args = {1, argv};
	m_fuse = fuse_new(&args, &operations, sizeof(operations), &m_tree);
	if (!m_fuse) {
		throw std::runtime_error("Failed to initialize FUSE");
	}

	if (fuse_mount(m_fuse, mountPoint.c_str())) {
		fuse_destroy(m_fuse);
		m_fuse = nullptr;
	}
}

vfs::Filesystem::~Filesystem() {
	if (m_fuse) {
		fuse_unmount(m_fuse);
		fuse_destroy(m_fuse);
	}
}

void vfs::Filesystem::shutdown() {
	if (m_fuse) {
		fuse_exit(m_fuse);
	}
}

int vfs::Filesystem::run() {
	if (m_fuse) {
		struct fuse_loop_config cfg = { .max_idle_threads = 10 };
		return fuse_loop_mt(m_fuse, &cfg);
	}

	return 1;
}
