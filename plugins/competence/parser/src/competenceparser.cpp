#include <competenceparser/competenceparser.h>

#include <boost/foreach.hpp>
#include <boost/process.hpp>

#include <chrono>
#include <memory>
#include <cctype>
#include <functional>

#include <model/commitdata.h>
#include <model/commitdata-odb.hxx>
#include <model/filecomprehension.h>
#include <model/filecomprehension-odb.hxx>
#include <model/sampledata.h>
#include <model/sampledata-odb.hxx>
#include <model/useremail.h>
#include <model/useremail-odb.hxx>
#include <model/filecommitloc.h>
#include <model/filecommitloc-odb.hxx>

#include <parser/sourcemanager.h>

#include <util/hash.h>
#include <util/logutil.h>
#include <util/odbtransaction.h>

namespace cc
{
namespace parser
{

std::map<std::pair<std::string, int>, int> CompetenceParser::_fileCommitLocData = {};

CompetenceParser::CompetenceParser(ParserContext& ctx_): AbstractParser(ctx_)
{
  srand(time(nullptr));

  int threadNum = _ctx.options["jobs"].as<int>();
  _pool = util::make_thread_pool<CommitJob>(
    threadNum, [this](CommitJob& job)
    {
      this->commitWorker(job);
    });

  setCompanyList();

  if (_ctx.options.count("commit-count"))
  {
    _maxCommitCount = _ctx.options["commit-count"].as<int>();

    if (_maxCommitCount < 1)
    {
      throw std::logic_error("Commit count is too small!");
    }
    LOG(info) << "[competenceparser] Commit history of " << _maxCommitCount << " commits will be parsed.";
    return;
  }
}

bool CompetenceParser::accept(const std::string& path_)
{
  std::string ext = boost::filesystem::extension(path_);
  return ext == ".competence";
}

bool CompetenceParser::parse()
{        
  for(const std::string& path :
    _ctx.options["input"].as<std::vector<std::string>>())
  {
    LOG(info) << "Competence parse path: " << path;

    boost::filesystem::path repoPath;

    auto rcb = getParserCallbackRepo(repoPath);

    try
    {
      util::iterateDirectoryRecursive(path, rcb);
    }
    catch (std::exception &ex_)
    {
      LOG(warning)
        << "Competence parser threw an exception: " << ex_.what();
    }
    catch (...)
    {
      LOG(warning)
        << "Competence parser failed with unknown exception!";
    }

    std::string repoId = std::to_string(util::fnvHash(repoPath.c_str()));
    RepositoryPtr repo = _gitOps.createRepository(repoPath);

    if (!repo)
      continue;

    util::OdbTransaction transaction(_ctx.db);
    transaction([&, this]
    {
      for (const model::FileComprehension& fc
        : _ctx.db->query<model::FileComprehension>())
        _ctx.db->erase(fc);

      for (const model::UserEmail& ue
        : _ctx.db->query<model::UserEmail>())
        _ctx.db->erase(ue);
    });

    commitSampling(path, repoPath);
    traverseCommits(path, repoPath);
    persistFileComprehensionData();

    auto pcb = persistNoDataFiles();

    try
    {
      util::iterateDirectoryRecursive(path, pcb);
    }
    catch (std::exception &ex_)
    {
      LOG(warning)
        << "Competence parser threw an exception: " << ex_.what();
    }
    catch (...)
    {
      LOG(warning)
        << "Competence parser failed with unknown exception!";
    }

    persistEmailAddress();
    setUserCompany();
  }

  return true;
}

util::DirIterCallback CompetenceParser::getParserCallbackRepo(
  boost::filesystem::path& repoPath_)
{
  return [&](const std::string& path_)
  {
    boost::filesystem::path path(path_);

    if (!boost::filesystem::is_directory(path) || ".git" != path.filename())
      return true;

    path = boost::filesystem::canonical(path);

    LOG(info) << "Competence parser found a git repo at: " << path;
    repoPath_ = path_;

    return true;
  };
}

void CompetenceParser::commitSampling(
  const std::string& root_,
  boost::filesystem::path& repoPath_)
{
  // Initiate repository.
  RepositoryPtr repo = _gitOps.createRepository(repoPath_);

  if (!_ctx.options.count("skip-forgetting"))
  {
    // Initiate walker.
    RevWalkPtr walker = _gitOps.createRevWalk(repo.get());
    git_revwalk_sorting(walker.get(), GIT_SORT_TOPOLOGICAL | GIT_SORT_TIME);
    git_revwalk_push_head(walker.get());

    git_oid oid;
    int allCommits = 0;
    while (git_revwalk_next(&oid, walker.get()) == 0)
      ++allCommits;

    LOG(info) << "[competenceparser] Sampling " << allCommits << " commits in git repository.";

    // TODO: nice function to determine sample size
    int sampleSize = 1;//(int)std::sqrt((double)allCommits);
    LOG(info) << "[competenceparser] Sample size is " << sampleSize << ".";

    RevWalkPtr walker2 = _gitOps.createRevWalk(repo.get());
    git_revwalk_sorting(walker2.get(), GIT_SORT_TOPOLOGICAL | GIT_SORT_TIME);
    git_revwalk_push_head(walker2.get());

    int commitCounter = 0;
    while (git_revwalk_next(&oid, walker2.get()) == 0)
    {
      ++commitCounter;
      if (commitCounter % sampleSize == 0)
      {
        CommitPtr commit = _gitOps.createCommit(repo.get(), oid);
        CommitJob job(repoPath_, root_, oid, commit.get(), commitCounter);
        sampleCommits(job);
      }
    }
    persistSampleData();
  }
}

void CompetenceParser::traverseCommits(
  const std::string& root_,
  boost::filesystem::path& repoPath_)
{
  // Initiate repository.
  RepositoryPtr repo = _gitOps.createRepository(repoPath_);

  if (!_ctx.options.count("skip-competence"))
  {
    // Initiate walker.
    RevWalkPtr fileLocWalker = _gitOps.createRevWalk(repo.get());
    git_revwalk_sorting(fileLocWalker.get(), GIT_SORT_TOPOLOGICAL | GIT_SORT_TIME);
    git_revwalk_push_head(fileLocWalker.get());

    git_oid fileLocOid;
    int commitCounter = 0;
    _commitCount = _maxCommitCount;
    while (git_revwalk_next(&fileLocOid, fileLocWalker.get()) == 0 && _maxCommitCount > commitCounter)
    {
      CommitPtr commit = _gitOps.createCommit(repo.get(), fileLocOid);
      //const git_oid* lofaszoid = git_commit_id(commit.get());
      //LOG(warning) << git_oid_tostr_s(lofaszoid);
      CommitJob job(repoPath_, root_, fileLocOid, commit.get(), ++commitCounter);
      collectFileLocData(job);
    }

    for (const auto& data : _fileCommitLocData)
    {
      auto iter = _fileLocData.find(data.first.first);
      if (iter != _fileLocData.end())
      {
        iter->second.push_back(data.second);
      }
      else
      {
        _fileLocData.insert(std::make_pair(data.first.first, std::vector<int>{data.second}));
      }
    }

    auto current = _fileLocData.begin(), next = _fileLocData.begin();
    while (next != _fileLocData.end())
    {
      current = next++;
      if (std::count(current->second.begin(), current->second.end(), 0) == current->second.size())
      {
        _fileLocData.erase(current);
      }
      else
      {
        std::sort(current->second.begin(), current->second.end());
        /*LOG(warning) << current->first << ", ";
        for (const int& i : current->second)
        {
          LOG(info) << i;
        }*/
      }
    }

    // Initiate walker.
    RevWalkPtr walker = _gitOps.createRevWalk(repo.get());
    git_revwalk_sorting(walker.get(), GIT_SORT_TOPOLOGICAL | GIT_SORT_TIME);
    git_revwalk_push_head(walker.get());

    std::vector<std::pair<git_oid, CommitPtr>> commits;
    git_oid oid;
    commitCounter = 0;
    while (git_revwalk_next(&oid, walker.get()) == 0 && _maxCommitCount > commitCounter)
    {
      // Retrieve commit.
      CommitPtr commit = _gitOps.createCommit(repo.get(), oid);
      CommitJob job(repoPath_, root_, oid, commit.get(), ++commitCounter);
      _pool->enqueue(job);
    }
    _pool->wait();
  }
}

int CompetenceParser::walkDeltaHunkCb(const char* root,
                                     const git_tree_entry* entry,
                                     void* payload)
{
  WalkDeltaHunkData* data = (WalkDeltaHunkData*)payload;
  const git_oid* entryId = git_tree_entry_id(entry);

  std::string deltaStr(data->delta->new_file.path);
  std::string entryName(root);
  entryName.append(git_tree_entry_name(entry));
  if (deltaStr.compare(entryName) == 0
      && git_tree_entry_filemode(entry) == GIT_FILEMODE_BLOB)
  {
    git_blob *blob = nullptr;
    git_blob_lookup(&blob, data->repo, entryId);
    std::string text((const char *) git_blob_rawcontent(blob));
    int lineNumber = std::count(text.begin(), text.end(), '\n');

    auto iter = _fileCommitLocData.find({data->delta->new_file.path, data->commitNumber});
    if (iter != _fileCommitLocData.end())
    {
      iter->second += lineNumber;
    }
    else
    {
      _fileCommitLocData.insert(std::make_pair(std::make_pair(data->delta->new_file.path, data->commitNumber), lineNumber));
    }
  }
}

void CompetenceParser::collectFileLocData(CommitJob& job)
{
  RepositoryPtr repo = _gitOps.createRepository(job._repoPath);

  //LOG(info) << "[competenceparser] Calculating file LOC in " << job._commitCounter
    //        << "/" << _commitCount << " of version control history.";

  // Retrieve parent of commit.
  CommitPtr parent = _gitOps.createParentCommit(job._commit);
  if (!parent)
    return;

  // Get git tree of both commits.
  TreePtr commitTree = _gitOps.createTree(job._commit);
  LOG(warning) << "Commit " << job._commitCounter << " id: "
               << git_oid_tostr_s(git_commit_id(job._commit));
  for (int i = 0; i < git_commit_parentcount(job._commit); ++i)
  {
    LOG(warning) << "Commit " << job._commitCounter << " parents: "
      << git_oid_tostr_s(git_commit_parent_id(job._commit, i));
  }
  TreePtr parentTree = _gitOps.createTree(parent.get());
  if (!commitTree || !parentTree)
    return;

  // Calculate diff of trees.
  DiffPtr diff = _gitOps.createDiffTree(repo.get(), parentTree.get(), commitTree.get());
  size_t numDeltas = git_diff_num_deltas(diff.get());
  if (numDeltas == 0)
    return;

  for (size_t i = 0; i < numDeltas; ++i)
  {
    const git_diff_delta* delta = git_diff_get_delta(diff.get(), i);
    WalkDeltaHunkData deltaData = { delta, job._commitCounter, repo.get() };
    git_tree_walk(commitTree.get(), GIT_TREEWALK_PRE, &CompetenceParser::walkDeltaHunkCb, &deltaData);
  }
}

void CompetenceParser::persistFileLocData()
{
  util::OdbTransaction transaction(_ctx.db);
  transaction([&, this]
  {
    /*for (const auto& data : _fileLocData)
    {
      model::FileCommitLoc fileLoc;
      fileLoc.file = std::get<0>(data)->id;
      fileLoc.totalLines = std::get<1>(data);
      fileLoc.commitDate = std::get<2>(data);
      _ctx.db->persist(fileLoc);
    }*/
  });
}

int CompetenceParser::walkCb(const char* root,
                             const git_tree_entry* entry,
                             void* payload)
{
  WalkData* data = static_cast<WalkData*>(payload);
  if (data->found)
  {
    //delete data;
    return 1;
  }

  std::string path(data->delta->new_file.path);
  std::string entryName(git_tree_entry_name(entry));
  std::string current(root + entryName);
  if (path.compare(0, current.size(), current) == 0
      && git_tree_entry_filemode(entry) == GIT_FILEMODE_BLOB)
  {
    if (data->isParent)
      data->basePath.append("/old/");
    else
      data->basePath.append("/new/");
    fs::create_directories(data->basePath);

    const git_oid* entryId = git_tree_entry_id(entry);
    git_blob *blob = NULL;
    git_blob_lookup(&blob, data->repo, entryId);
    std::ofstream currentFile;
    std::replace(current.begin(), current.end(), '/', '_');
    currentFile.open(data->basePath.string() + current);
    currentFile.write((const char*)git_blob_rawcontent(blob), (size_t) git_blob_rawsize(blob));
    currentFile.close();
    data->found = true;
    //delete blob;
    //delete entryId;
    //delete data;
    return -1;
  }
  else
  {
    //delete data;
    return 0;
  }
}

std::string CompetenceParser::plagiarismCommand(const std::string& extension)
{
  std::string command("java -jar ../lib/java/jplag-2.12.1.jar -t 1 -vq -l ");

  std::vector<std::string> cppExt = { ".cpp", ".CPP", ".cxx", ".CXX", ".c++", ".C++",
                                      ".c", ".C", ".cc", ".CC", ".h", ".H",
                                      ".hpp", ".HPP", ".hh", ".HH" };
  std::vector<std::string> txtExt = { ".TXT", ".txt", ".ASC", ".asc", ".TEX", ".tex" };
  if (std::find(cppExt.begin(), cppExt.end(), extension) != cppExt.end())
    command.append("c/c++ ");
  else if (extension == ".cs" || extension == ".CS")
    command.append("c#-1.2");
  else if (extension == ".java" || extension == ".JAVA")
    command.append("java19 ");
  else if (extension == ".py")
    command.append("python3 ");
  else if (std::find(txtExt.begin(), txtExt.end(), extension) != txtExt.end())
    command.append("text ");
  else
    return "";

  return command;
}

void CompetenceParser::commitWorker(CommitJob& job)
{
  RepositoryPtr repo = _gitOps.createRepository(job._repoPath);

  LOG(info) << "[competenceparser] Parsing " << job._commitCounter << "/" << _commitCount << " of version control history.";

  const git_signature* commitAuthor = git_commit_author(job._commit);

  if (commitAuthor && !std::isgraph(commitAuthor->name[0]))
  {
    LOG(info) << "[competenceparser] " << job._commitCounter << "/" << _commitCount << " commit author is invalid.";
    ++faultyCommits;
    return;
  }

  basePath = boost::filesystem::path(_ctx.options["workspace"].as<std::string>());
  basePath.append(_ctx.options["name"].as<std::string>() + "/competence/");

  // Calculate elapsed time in full months since current commit.
  std::time_t elapsed = std::chrono::system_clock::to_time_t(
    std::chrono::system_clock::now()) - commitAuthor->when.time;
  //double months = elapsed / (double) (secondsInDay * daysInMonth);
  double months = elapsed / (double)secondsInDay / 7;

  //if (_maxCommitHistoryLength > 0 && months > _maxCommitHistoryLength)
    //return;

  // Retrieve parent of commit.
  CommitPtr parent = _gitOps.createParentCommit(job._commit);

  if (!parent)
  {
    ++faultyCommits;
    return;
  }

  // Get git tree of both commits.
  TreePtr commitTree = _gitOps.createTree(job._commit);
  TreePtr parentTree = _gitOps.createTree(parent.get());

  if (!commitTree || !parentTree)
  {
    ++faultyCommits;
    return;
  }

  // Calculate diff of trees.
  DiffPtr diff = _gitOps.createDiffTree(repo.get(), parentTree.get(), commitTree.get());

  // Loop through each delta.
  size_t num_deltas = git_diff_num_deltas(diff.get());
  if (num_deltas == 0)
  {
    ++faultyCommits;
    return;
  }

  // Copy all modified files to workspace dir.
  // Old and new version goes to separate directories.
  fs::path outPath(basePath);
  outPath.append(std::to_string(job._commitCounter));
  fs::create_directories(outPath);

  std::vector<const git_diff_delta*> deltas;
  bool hasModifiedFiles = false;
  std::map<fs::path, double> plagValues;
  for (size_t j = 0; j < num_deltas; ++j)
  {
    const git_diff_delta* delta = git_diff_get_delta(diff.get(), j);

    // Walk diff tree to find modified file.
    if (delta->status == GIT_DELTA_MODIFIED)
    {
      hasModifiedFiles = true;
      WalkData commitData = {delta, repo.get(), outPath, false };
      git_tree_walk(commitTree.get(), GIT_TREEWALK_PRE, &CompetenceParser::walkCb, &commitData);

      WalkData parentData = {delta, repo.get(), outPath, true };
      git_tree_walk(parentTree.get(), GIT_TREEWALK_PRE, &CompetenceParser::walkCb, &parentData);
    }

    if (delta->status == GIT_DELTA_ADDED)
    {
      std::string replacedStr(delta->new_file.path);
      std::replace(replacedStr.begin(), replacedStr.end(), '/', '_');
      plagValues.insert(std::make_pair(replacedStr, 0));
    }
  }

  fs::path newPath(outPath);
  newPath.append("/new/");
  fs::path oldPath(outPath);
  oldPath.append("/old/");

  // Determine plagiarism command based on file extension.
  if (hasModifiedFiles && fs::exists(newPath) && fs::exists(oldPath))
  {
    int allFiles = 0, notSupported = 0;
    for (const auto &f : fs::directory_iterator(newPath))
    {
      ++allFiles;
      std::string command = plagiarismCommand(fs::extension(f.path()));

      if (command.empty())
      {
        LOG(info) << "Plagiarism detector does not support file type: " << f.path().filename();
        ++notSupported;
        continue;
      }

      command.append("-c " + f.path().string() + " ");
      const fs::recursive_directory_iterator end;
      const auto it = std::find_if(fs::recursive_directory_iterator(oldPath), end,
                                   [&f](const fs::directory_entry &entry)
                                   {
                                     return entry.path().filename() == f.path().filename();
                                   });
      if (it == end)
      {
        //++faultyCommits;
        // why return???????
        LOG(warning) << "amugy ebbe belefutottam";
        return;
      }

      command.append(it->path().string());
      fs::path logPath(outPath);
      std::future<std::string> log;
      boost::process::system(command, boost::process::std_out > log);

      // Retrieve results from JPlag log (stored in memory).
      std::string logStr = log.get();
      auto index = logStr.find_last_of(' ');

      try
      {
        // std::stod may throw in case of JPlag failure.
        double value = std::stod(logStr.substr(++index));
        plagValues.insert(std::make_pair(f.path().filename(), value));
      }
      catch (std::exception &ex)
      {
        LOG(warning) << "Plagiarism detection unsuccessful (" << ex.what() << "): " << logStr;
      }
    }
    if (allFiles == notSupported)
      ++allNotSupported;
    //for (const auto &p : plagValues)
      //LOG(info) << p.first << ": " << p.second;

    fs::remove_all(outPath);
  }
  else
  {
    LOG(warning) << "Commit " << job._commitCounter << " either has no modified files or "
      " has relevant modified files but "
      " they couldn't be copied to the workspace directory.";
  }

  // Analyse every file that was affected by the commit.
  int hundredpercent = 0, notSaved = 0;
  for (size_t j = 0; j < num_deltas; ++j)
  {
    const git_diff_delta* delta = git_diff_get_delta(diff.get(), j);
    git_diff_file diffFile = delta->new_file;

    model::FilePtr file = _ctx.srcMgr.getFile(job._root + "/" + diffFile.path);
    if (!file)
    {
      ++notSaved;
      continue;
    }

    double currentPlagValue = -1.0;
    if (hasModifiedFiles)
    {
      if (delta->status == GIT_DELTA_MODIFIED
          || delta->status == GIT_DELTA_ADDED)
      {
        std::string pathReplace(delta->new_file.path);
        std::replace(pathReplace.begin(), pathReplace.end(), '/', '_');
        auto iter = plagValues.find(pathReplace);
        if (iter != plagValues.end())
          currentPlagValue = iter->second;
        else
        {
          LOG(warning) << "did not find file: " << pathReplace;
        }
      }

      if (currentPlagValue >= plagThreshold)
      {
        LOG(info) << "Commit " << job._commitCounter << "/" << _commitCount
                  << " did not reach the threshold: " << currentPlagValue;
        ++hundredpercent;
        ++notSaved;
        continue;
      }
    }

    float totalLines = 0;

    // Get blame for file.
    BlameOptsPtr opt = _gitOps.createBlameOpts(job._oid);
    BlamePtr blame = _gitOps.createBlame(repo.get(), diffFile.path, opt.get());

    if (!blame)
    {
      ++notSaved;
      continue;
    }

    // Store the current number of blame lines for each user.
    std::map<UserEmail, UserBlameLines> userBlame;
    std::uint32_t blameHunkCount = git_blame_get_hunk_count(blame.get());
    for (std::uint32_t i = 0; i < blameHunkCount; ++i)
    {
      const git_blame_hunk* hunk = git_blame_get_hunk_byindex(blame.get(), i);

      if (!git_oid_equal(&hunk->final_commit_id, &job._oid))
      {
        totalLines += hunk->lines_in_hunk;
        continue;
      }

      GitBlameHunk blameHunk;
      blameHunk.linesInHunk = hunk->lines_in_hunk;
      if (hunk->final_signature)
      {
        blameHunk.finalSignature.email = hunk->final_signature->email;
      }
      //else
        //continue;
        // TODO
        // git_oid_iszero is deprecated.
        // It should be replaced with git_oid_is_zero in case of upgrading libgit2.
      else if (!git_oid_iszero(&hunk->final_commit_id))
      {
        CommitPtr newCommit = _gitOps.createCommit(repo.get(), hunk->final_commit_id);
        const git_signature* author = git_commit_author(newCommit.get());
        blameHunk.finalSignature.email = author->email;
      }

      auto it = userBlame.find(std::string(blameHunk.finalSignature.email));
      if (it != userBlame.end())
      {
        it->second += blameHunk.linesInHunk;
      }
      else
      {
        userBlame.insert(std::make_pair(std::string(blameHunk.finalSignature.email),
                                        blameHunk.linesInHunk));
        _calculateFileData.lock();
        _emailAddresses.insert(blameHunk.finalSignature.email);
        _calculateFileData.unlock();
      }

      totalLines += blameHunk.linesInHunk;
    }
    persistCommitData(file->id, userBlame, totalLines, commitAuthor->when.time);
    _calculateFileData.lock();

    for (const auto& pair : userBlame)
    {
      if (pair.second != 0)
      {
        // Calculate the retained memory depending on the elapsed time.
        //double percentage = pair.second / totalLines * std::exp(-months) * 100;
        auto fileLocIter = _fileLocData.find(delta->new_file.path);
        double strength;
        if (currentPlagValue == -1.0)
        {
          strength = (double)pair.second / (double)totalLines;
        }
        else
        {
          strength = 100 - currentPlagValue;
        }
        /*else if (fileLocIter == _fileLocData.end())
        {
          strength = (double)pair.second; // / (double)totalLines;
        }
        else
        {
          const auto medianIter = fileLocIter->second.begin() + fileLocIter->second.size() / 2;
          std::nth_element(fileLocIter->second.begin(), medianIter , fileLocIter->second.end());
          strength = (double)pair.second; // / (double)*medianIter;
          //strength = (double)pair.second; // / totalLines;
          LOG(info) << "median: " << *medianIter << ", strength: " << strength;
        }*/

        //double percentage = std::exp(-(months/strength)) * 100;
        //double percentage = strength * 100;
        double percentage = strength;
        //LOG(info) << percentage;
        //LOG(info) << file->path << ": " << percentage

        auto fileIter = _fileEditions.end();
        for (auto it = _fileEditions.begin(); it != _fileEditions.end(); ++it)
          if (it->_file.get()->id == file.get()->id)
          {
            fileIter = it;
            break;
          }

        if (fileIter != _fileEditions.end())
        {
          auto userIter = fileIter->_editions.find(pair.first);
          if (userIter != fileIter->_editions.end())
          {
            if (userIter->second.first < percentage)
            {
              userIter->second.first = percentage;
              ++(userIter->second.second);
            }
          }
          else
          {
            fileIter->_editions.insert(std::make_pair(pair.first, std::make_pair(percentage, 1)));
          }
        }
        else
        {
          FileEdition fe { file, std::map<UserEmail, FileDataPair>() };
          fe._editions.insert(std::make_pair(pair.first, std::make_pair(percentage, 1)));
          _fileEditions.push_back(fe);
       }
      }
    }
    _calculateFileData.unlock();
  }

  if (num_deltas == hundredpercent)
  {
    LOG(warning) << job._commitCounter << "/" << _commitCount << ", " << git_oid_tostr_s(git_commit_id(job._commit))
    << " had all 100 files.";
  }
  LOG(warning) << "not saved: " << notSaved;
  LOG(info) << "[competenceparser] Finished parsing " << job._commitCounter << "/" << _commitCount;
}

void CompetenceParser::persistCommitData(
  const model::FileId& fileId_,
  const std::map<UserEmail, UserBlameLines>& userBlame_,
  const float totalLines_,
  const std::time_t& commitDate_)
{
  util::OdbTransaction transaction(_ctx.db);
  transaction([&, this]
  {
    for (const auto& blame : userBlame_)
    {
      model::CommitData commitData;
      commitData.file = fileId_;
      commitData.committerEmail = blame.first;
      commitData.committedLines = blame.second;
      commitData.totalLines = totalLines_;
      commitData.commitDate = commitDate_;
      _ctx.db->persist(commitData);
    }
  });
}

void CompetenceParser::persistFileComprehensionData()
{
  for (const auto& edition : _fileEditions)
  {
    LOG(info) << "[competenceparser] " << edition._file->path << ": ";
    for (const auto& pair : edition._editions)
    {
      util::OdbTransaction transaction(_ctx.db);
      transaction([&, this]
      {
        model::FileComprehension fileComprehension;
        fileComprehension.file = edition._file->id;
        fileComprehension.userEmail = pair.first;
        //LOG(info) << pair.second.first;
        //fileComprehension.repoRatio = std::exp(-pair.second.first) * 100; // / pair.second.second;
        fileComprehension.repoRatio = pair.second.first;
        fileComprehension.userRatio = fileComprehension.repoRatio.get();
        fileComprehension.inputType = model::FileComprehension::InputType::REPO;
        _ctx.db->persist(fileComprehension);

        LOG(info) << pair.first << " " << fileComprehension.repoRatio.get() << "%";
      });
    }
  }
}

util::DirIterCallback CompetenceParser::persistNoDataFiles()
{
  return [this](const std::string& currPath_)
  {
    boost::filesystem::path path
    = boost::filesystem::canonical(currPath_);

    if (boost::filesystem::is_regular_file(path) &&
        !fileEditionContains(currPath_))
      _ctx.srcMgr.getFile(currPath_);

    return true;
  };
}

void CompetenceParser::sampleCommits(CommitJob& job_)
{
  RepositoryPtr repo = _gitOps.createRepository(job_._repoPath);

  // Retrieve parent of commit.
  CommitPtr parent = _gitOps.createParentCommit(job_._commit);

  if (!parent)
    return;

  // Get git tree of both commits.
  TreePtr commitTree = _gitOps.createTree(job_._commit);
  TreePtr parentTree = _gitOps.createTree(parent.get());

  if (!commitTree || !parentTree)
    return;

  // Calculate diff of trees.
  DiffPtr diff = _gitOps.createDiffTree(repo.get(), parentTree.get(), commitTree.get());

  // Loop through each delta.
  size_t num_deltas = git_diff_num_deltas(diff.get());
  if (num_deltas == 0)
    return;

  // Analyse every file that was affected by the commit.
  for (size_t j = 0; j < num_deltas; ++j)
  {
    const git_diff_delta *delta = git_diff_get_delta(diff.get(), j);
    git_diff_file diffFile = delta->new_file;
    model::FilePtr file = _ctx.srcMgr.getFile(job_._root + "/" + diffFile.path);
    if (!file)
      continue;

    auto iter = _commitSample.find(file);
    if (iter == _commitSample.end())
      _commitSample.insert({file, 1});
    else
      ++(iter->second);
  }
}

void CompetenceParser::persistSampleData()
{
  for (const auto& pair : _commitSample)
  {
    util::OdbTransaction transaction(_ctx.db);
    transaction([&, this]
    {
      model::SampleData sample;
      sample.file = pair.first->id;
      sample.occurrences = pair.second;
      _ctx.db->persist(sample);
    });
  }
}

bool CompetenceParser::fileEditionContains(const std::string& path_)
{
  for (const auto& fe : _fileEditions)
    if (fe._file->path == path_)
      return true;

  return false;
}


void CompetenceParser::setUserCompany()
{
  util::OdbTransaction transaction(_ctx.db);
  transaction([&, this]
  {
    auto users = _ctx.db->query<model::UserEmail>();
    for (auto& user : users)
      for (const auto& p : _companyList)
        if (user.email.find(p.first) != std::string::npos)
        {
          user.company = p.second;
          _ctx.db->update(user);
          break;
        }
  });
}

void CompetenceParser::persistEmailAddress()
{
  util::OdbTransaction transaction(_ctx.db);
  transaction([&, this]
  {
    auto query = _ctx.db->query<model::UserEmail>();
    for (const auto& user : query)
      if (_emailAddresses.find(user.email) != _emailAddresses.end())
        _emailAddresses.erase(user.email);
  });

  for (const auto& address : _emailAddresses)
  {
    util::OdbTransaction transaction(_ctx.db);
    transaction([&, this]
    {
      model::UserEmail userEmail;
      userEmail.email = address;
      _ctx.db->persist(userEmail);
    });
  }
}

void CompetenceParser::setCompanyList()
{
  _companyList.insert({"amd.com", "AMD"});
  _companyList.insert({"apple.com", "Apple"});
  _companyList.insert({"arm.com", "ARM"});
  _companyList.insert({"ericsson.com", "Ericsson"});
  _companyList.insert({"fujitsu.com", "Fujitsu"});
  _companyList.insert({"harvard.edu", "Harvard"});
  _companyList.insert({"huawei.com", "Huawei"});
  _companyList.insert({"ibm.com", "IBM"});
  _companyList.insert({"inf.elte.hu", "ELTE FI"});
  _companyList.insert({"intel.com", "Intel"});
  _companyList.insert({"microsoft.com", "Microsoft"});
  _companyList.insert({"nokia.com", "Nokia"});
  _companyList.insert({"oracle.com", "Oracle"});
  _companyList.insert({"sony.com", "Sony"});
  _companyList.insert({"samsung.com", "Samsung"});
}

CompetenceParser::~CompetenceParser()
{
  git_libgit2_shutdown();
}

/* These two methods are used by the plugin manager to allow dynamic loading
   of CodeCompass Parser plugins. Clang (>= version 6.0) gives a warning that
   these C-linkage specified methods return types that are not proper from a
   C code.

   These codes are NOT to be called from any C code. The C linkage is used to
   turn off the name mangling so that the dynamic loader can easily find the
   symbol table needed to set the plugin up.
*/
// When writing a plugin, please do NOT copy this notice to your code.
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wreturn-type-c-linkage"
extern "C"
{
  boost::program_options::options_description getOptions()
  {
    boost::program_options::options_description description("Competence Plugin");

    description.add_options()
      ("commit-count", po::value<int>(),
        "This is a threshold value. It is the number of commits the competence parser"
        "will process if value is given. If both commit-history and commit-count is given,"
        "the value of commit-count will be the threshold value.")
      ("skip-forgetting",
        "If this flag is given, the competence parser will skip the file competence anaysis.")
      ("skip-competence",
        "If this flag is given, the competence parser will only execute the file"
        "frequency calculation, and skip the file competence anaysis.");

    return description;
  }

  std::shared_ptr<CompetenceParser> make(ParserContext& ctx_)
  {
    return std::make_shared<CompetenceParser>(ctx_);
  }
}
#pragma clang diagnostic pop

} // parser
} // cc
