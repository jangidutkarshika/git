#include "cache.h"
#include "bundle-uri.h"
#include "bundle.h"
#include "object-store.h"
#include "refs.h"
#include "run-command.h"

static void find_temp_filename(struct strbuf *name)
{
	int fd;
	/*
	 * Find a temporray filename that is available. This is briefly
	 * racy, but unlikely to collide.
	 */
	fd = odb_mkstemp(name, "bundles/tmp_uri_XXXXXX");
	if (fd < 0)
		die(_("failed to create temporary file"));
	close(fd);
	unlink(name->buf);
}

static int copy_uri_to_file(const char *uri, const char *file)
{
	/* Copy as a file */
	return copy_file(file, uri, 0444);
}

static int unbundle_from_file(struct repository *r, const char *file)
{
	int result = 0;
	int bundle_fd;
	struct bundle_header header = BUNDLE_HEADER_INIT;
	struct strvec extra_index_pack_args = STRVEC_INIT;
	struct string_list_item *refname;
	struct strbuf bundle_ref = STRBUF_INIT;
	size_t bundle_prefix_len;

	if ((bundle_fd = read_bundle_header(file, &header)) < 0)
		return 1;

	result = unbundle(r, &header, bundle_fd, &extra_index_pack_args);

	/*
	 * Convert all refs/heads/ from the bundle into refs/bundles/
	 * in the local repository.
	 */
	strbuf_addstr(&bundle_ref, "refs/bundles/");
	bundle_prefix_len = bundle_ref.len;

	for_each_string_list_item(refname, &header.references) {
		struct object_id *oid = refname->util;
		struct object_id old_oid;
		const char *branch_name;
		int has_old;

		if (!skip_prefix(refname->string, "refs/heads/", &branch_name))
			continue;

		strbuf_setlen(&bundle_ref, bundle_prefix_len);
		strbuf_addstr(&bundle_ref, branch_name);

		has_old = !read_ref(bundle_ref.buf, &old_oid);
		update_ref("fetched bundle", bundle_ref.buf, oid,
			   has_old ? &old_oid : NULL,
			   REF_SKIP_OID_VERIFICATION,
			   UPDATE_REFS_MSG_ON_ERR);
	}

	bundle_header_release(&header);
	return result;
}

int fetch_bundle_uri(struct repository *r, const char *uri)
{
	int result = 0;
	struct strbuf filename = STRBUF_INIT;

	find_temp_filename(&filename);
	if ((result = copy_uri_to_file(uri, filename.buf))) {
		error(_("failed to download bundle from URI '%s'"), uri);
		goto cleanup;
	}

	if ((result = !is_bundle(filename.buf, 0))) {
		error(_("file at URI '%s' is not a bundle"), uri);
		goto cleanup;
	}

	if ((result = unbundle_from_file(r, filename.buf)))
		goto cleanup;

cleanup:
	unlink(filename.buf);
	strbuf_release(&filename);
	return result;
}
