#include "root.hpp"
#include <core/3d/gl.hpp>
#include <core/api.hpp>
#include <core/util/gui.hpp>
#include <core/util/timestamp.hpp>
#include <frontend/editor/EditorWindow.hpp>
#include <frontend/widgets/changelog.hpp>
#include <frontend/widgets/fps.hpp>
#include <frontend/widgets/fullscreen.hpp>
#include <frontend/widgets/theme_editor.hpp>
#include <fstream>
#include <imgui_markdown.h>
#include <oishii/reader/binary_reader.hxx>
#include <oishii/writer/binary_writer.hxx>
#include <pfd/portable-file-dialogs.h>
// Experimental conversion
#include <plugins/j3d/Scene.hpp>

#ifdef __EMSCRIPTEN__
#include <emscripten.h>
#endif

namespace riistudio::frontend {

#ifdef _WIN32
static void GlCallback(GLenum source, GLenum type, GLuint id, GLenum severity,
                       GLsizei length, const GLchar* message,
                       GLvoid* userParam) {
  printf("%s\n", message);
}
#endif

void RootWindow::draw() {
  fileHostProcess();

  ImGui::PushID(0);
  if (BeginFullscreenWindow("##RootWindow", getOpen())) {
    if (mThemeUpdated) {
      mTheme.setThemeEx(mCurTheme);
      mThemeUpdated = false;
    }
    ImGui::GetIO().FontGlobalScale = mFontGlobalScale;

    ImGui::SetWindowFontScale(1.1f);
    if (!hasChildren()) {
      ImGui::Text("Drop a file to edit.");
    }
    // ImGui::Text(
    //     "Note: For this early alpha, file saving is temporarily disabled.");
    ImGui::SetWindowFontScale(1.0f);
    dockspace_id = ImGui::GetID("DockSpaceWidget");

    ImGuiID dock_main_id = dockspace_id;
    while (mAttachEditorsQueue.size()) {
      const std::string& ed_id = mAttachEditorsQueue.front();

      // ImGui::DockBuilderRemoveNode(dockspace_id); // Clear out existing
      // layout ImGui::DockBuilderAddNode(dockspace_id); // Add empty node
      //
      //
      // ImGuiID dock_up_id = ImGui::DockBuilderSplitNode(dock_main_id,
      // ImGuiDir_Up, 0.05f, nullptr, &dock_main_id);
      // ImGui::DockBuilderDockWindow(ed_id.c_str(), dock_up_id);
      //
      //
      // ImGui::DockBuilderFinish(dockspace_id);
      mAttachEditorsQueue.pop();
    }

    ImGui::DockSpace(dockspace_id, ImVec2(0.0f, 0.0f), 0);

    EditorWindow* ed =
        getActive() ? dynamic_cast<EditorWindow*>(getActive()) : nullptr;
    assert(ed || !hasChildren());

    if (ImGui::BeginMenuBar()) {
      if (ImGui::BeginMenu("File")) {
#ifdef _WIN32
        if (ImGui::MenuItem("Open")) {
          openFile();
        }
#endif
        if (ImGui::MenuItem("Save")) {

          if (ed) {
            DebugReport("Attempting to save to %s\n",
                        ed->getFilePath().c_str());
            if (!ed->getFilePath().empty())
              save(ed->getFilePath());
            else
              saveAs();
          } else {
            printf("Cannot save.. nothing has been opened.\n");
          }
        }
#ifdef _WIN32
        if (ImGui::MenuItem("Save As")) {
          if (ed)
            saveAs();
          else
            printf("Cannot save.. nothing has been opened.\n");
        }
#endif
        ImGui::EndMenu();
      }
      if (ImGui::BeginMenu("Windows")) {
        //	if (ed && ed->mWindowCollection)
        //	{
        //		for (int i = 0; i < ed->mWindowCollection->getNum();
        //++i)
        //		{
        //			if
        //(ImGui::MenuItem(ed->mWindowCollection->getName(i)))
        //			{
        //				ed->attachWindow(ed->mWindowCollection->spawn(i),
        // ed->mId);
        //			}
        //		}
        //	}
        ImGui::EndMenu();
      }

      if (ImGui::BeginMenu("Settings")) {
        bool _vsync = vsync;
        ImGui::Checkbox("VSync", &_vsync);

        if (_vsync != vsync) {
          setVsync(_vsync);
          vsync = _vsync;
        }

        mThemeUpdated |= DrawThemeEditor(mCurTheme, mFontGlobalScale, nullptr);

#ifdef BUILD_DEBUG
        ImGui::Checkbox("ImGui Demo", &bDemo);
#endif

        ImGui::EndMenu();
      }

      if (bDemo)
        ImGui::ShowDemoWindow(&bDemo);

#ifndef BUILD_DIST
      if (ImGui::BeginMenu("Experimental")) {
        if (ImGui::MenuItem("Convert to BMD") && ed != nullptr) {
          const libcube::Scene* from_root =
              dynamic_cast<libcube::Scene*>(ed->mState.get());
          auto from_models = from_root->getModels();
          auto from_textures = from_root->getTextures();

          std::unique_ptr<kpi::INode> bmd_state{dynamic_cast<kpi::INode*>(
              SpawnState(typeid(j3d::Collection).name()).release())};

          j3d::Collection& bmd_col =
              *dynamic_cast<j3d::Collection*>(bmd_state.get());

          for (auto& from_model : from_models) {
            auto& bmd_model = bmd_col.getModels().add();
            // info
            // Bufs (automate)
            bmd_model.mBufs.norm.mQuant.comp.normal =
                libcube::gx::VertexComponentCount::Normal::xyz;
            // drawmtx (skip)
            auto& mtx = bmd_model.mDrawMatrices.emplace_back();
            mtx.mWeights.emplace_back(0, 1.0f);
            // materials
            {
              const auto from_mats = from_model.getMaterials();
              for (int m_i = 0; m_i < from_mats.size(); ++m_i) {
                auto& mat = bmd_model.getMaterials().add();
                mat.id = m_i;

                auto& md = mat.getMaterialData();

                mat.getMaterialData() = from_mats[m_i].getMaterialData();

                auto before = mat.getMaterialData().samplers;
                md.samplers = {};
                for (int i = 0; i < before.size(); ++i) {
                  auto simp =
                      std::make_unique<j3d::MaterialData::J3DSamplerData>();
                  static_cast<libcube::GCMaterialData::SamplerData&>(
                      *simp.get()) =
                      static_cast<const libcube::GCMaterialData::SamplerData&>(
                          *before[i].get());
                  md.samplers.push_back(std::move(simp));
                }
                mat.indEnabled = mat.getMaterialData().info.nIndStage > 0;
              }
            }
            // joints
            {
              auto from_joints = from_model.getBones();
              for (int m_i = 0; m_i < from_joints.size(); ++m_i) {
                auto& joint = bmd_model.getBones().add();
                joint.id = m_i;
                from_joints[m_i].copy(joint);
              }
            }
            // shapes
            {
              auto from_shapes = from_model.getMeshes();
              for (int m_i = 0; m_i < from_shapes.size(); ++m_i) {
                auto& from_shape = from_shapes[m_i];
                const auto vcd = from_shape.getVcd();

                auto& bmd_shape = bmd_model.getMeshes().add();
                bmd_shape.id = m_i;
                bmd_shape.mVertexDescriptor = vcd;
                for (auto& e : bmd_shape.mVertexDescriptor.mAttributes) {
                  e.second = libcube::gx::VertexAttributeType::Short;
                }
                bmd_shape.mVertexDescriptor
                    .calcVertexDescriptorFromAttributeList();

                for (int i = 0; i < from_shape.getNumMatrixPrimitives(); ++i) {
                  auto& bmd_mp = bmd_shape.mMatrixPrimitives.emplace_back();
                  bmd_mp.mCurrentMatrix = 0;
                  bmd_mp.mDrawMatrixIndices.push_back(0);
                  // No multi mtx yet
                  for (int j = 0;
                       j < from_shape.getMatrixPrimitiveNumIndexedPrimitive(i);
                       ++j) {
                    const auto& prim =
                        from_shape.getMatrixPrimitiveIndexedPrimitive(i, j);
                    auto& p = bmd_mp.mPrimitives.emplace_back(prim);
                    // Remap vtx indices
                    for (auto& v : p.mVertices) {
                      for (u32 x = 0;
                           x < (u32)libcube::gx::VertexAttribute::Max; ++x) {
                        if (!(vcd.mBitfield & (1 << x)))
                          continue;
                        auto& bufs = bmd_model.mBufs;
                        switch (static_cast<libcube::gx::VertexAttribute>(x)) {
                        case libcube::gx::VertexAttribute::
                            PositionNormalMatrixIndex:
                        case libcube::gx::VertexAttribute::Texture0MatrixIndex:
                        case libcube::gx::VertexAttribute::Texture1MatrixIndex:
                        case libcube::gx::VertexAttribute::Texture2MatrixIndex:
                        case libcube::gx::VertexAttribute::Texture3MatrixIndex:
                        case libcube::gx::VertexAttribute::Texture4MatrixIndex:
                        case libcube::gx::VertexAttribute::Texture5MatrixIndex:
                        case libcube::gx::VertexAttribute::Texture6MatrixIndex:
                        case libcube::gx::VertexAttribute::Texture7MatrixIndex:
                          break;
                        case libcube::gx::VertexAttribute::Position: {
                          auto pos = from_shape.getPos(
                              v[libcube::gx::VertexAttribute::Position]);
                          auto found = std::find(bufs.pos.mData.begin(),
                                                 bufs.pos.mData.end(), pos);
                          if (found == bufs.pos.mData.end()) {
                            bmd_model.mBufs.pos.mData.push_back(pos);
                            v[libcube::gx::VertexAttribute::Position] =
                                bmd_model.mBufs.pos.mData.size() - 1;
                          } else {
                            v[libcube::gx::VertexAttribute::Position] =
                                found - bufs.pos.mData.begin();
                          }
                          break;
                        }
                        case libcube::gx::VertexAttribute::Color0: {
                          const auto scolor = from_shape.getClr(
                              0, v[libcube::gx::VertexAttribute::Color0]);
                          libcube::gx::Color clr;
                          clr.r = roundf(scolor[0] * 255.0f);
                          clr.g = roundf(scolor[1] * 255.0f);
                          clr.b = roundf(scolor[2] * 255.0f);
                          clr.a = roundf(scolor[3] * 255.0f);
                          bmd_model.mBufs.color[0].mData.push_back(clr);
                          v[libcube::gx::VertexAttribute::Color0] =
                              bmd_model.mBufs.color[0].mData.size() - 1;
                          break;
                        }
                        case libcube::gx::VertexAttribute::TexCoord0:
                        case libcube::gx::VertexAttribute::TexCoord1:
                        case libcube::gx::VertexAttribute::TexCoord2:
                        case libcube::gx::VertexAttribute::TexCoord3:
                        case libcube::gx::VertexAttribute::TexCoord4:
                        case libcube::gx::VertexAttribute::TexCoord5:
                        case libcube::gx::VertexAttribute::TexCoord6:
                        case libcube::gx::VertexAttribute::TexCoord7: {
                          const auto chan =
                              x - static_cast<int>(
                                      libcube::gx::VertexAttribute::TexCoord0);
                          const auto attr =
                              static_cast<libcube::gx::VertexAttribute>(x);
                          const auto data = from_shape.getUv(chan, v[attr]);

                          bmd_model.mBufs.uv[chan].mData.push_back(data);
                          v[static_cast<libcube::gx::VertexAttribute>(x)] =
                              bmd_model.mBufs.uv[chan].mData.size() - 1;

                          break;
                        }
                        case libcube::gx::VertexAttribute::Normal:
                          bmd_model.mBufs.norm.mData.push_back(
                              from_shape.getNrm(
                                  v[libcube::gx::VertexAttribute::Normal]));
                          v[libcube::gx::VertexAttribute::Normal] =
                              bmd_model.mBufs.norm.mData.size() - 1;
                          break;
                        default:
                          throw "Invalid vtx attrib";
                          break;
                        }
                      }
                    }
                  }
                }
              }
            }
          }
          // textures
          for (const auto& from_texture : from_textures) {
            auto& bt = bmd_col.getTextures().add();
            bt.mName = from_texture.getName();
            bt.mFormat = from_texture.getTextureFormat();
            bt.bTransparent = false; // TODO
            bt.mWidth = from_texture.getWidth();
            bt.mHeight = from_texture.getHeight();
            bt.mPaletteFormat = from_texture.getPaletteFormat();
            bt.nPalette = 0;
            bt.ofsPalette = 0;
            bt.mMinLod = 0; // TODO: Verify
            bt.mMaxLod = from_texture.getMipmapCount() + 1;
            bt.mMipmapLevel = from_texture.getMipmapCount() + 1;
            bt.mData.resize(from_texture.getEncodedSize(true));
            memcpy(bt.mData.data(), from_texture.getData(), bt.mData.size());
            // TODO: Copy
            // bmd_texture.get()
          }

          attachEditorWindow(std::make_unique<EditorWindow>(
              std::move(bmd_state), "__conv.bmd"));
        }
        ImGui::EndMenu();
      }
#endif

      ImGui::SameLine(ImGui::GetWindowWidth() - 60);
      DrawFps();

      ImGui::EndMenuBar();
    }

    DrawChangeLog(&mShowChangeLog);

    drawChildren();
  }
  // Handle popups
  EndFullscreenWindow();
  ImGui::PopID();
}
void RootWindow::onFileOpen(FileData data, OpenFilePolicy policy) {
  printf("Opening file: %s\n", data.mPath.c_str());
  // TODO: Not ideal..
  std::vector<u8> vec(data.mLen);
  memcpy(vec.data(), data.mData.get(), data.mLen);

  oishii::DataProvider provider(std::move(vec), data.mPath);
  oishii::BinaryReader reader(provider.slice());
  auto importer = SpawnImporter(data.mPath, provider.slice());

  if (!importer.second) {
    printf("Cannot spawn importer..\n");
    return;
  }
  if (!IsConstructible(importer.first)) {
    printf("Non constructable state.. find parents\n");

    const auto children = GetChildrenOfType(importer.first);
    if (children.empty()) {
      printf("No children. Cannot construct.\n");
      return;
    }
    assert(/*children.size() == 1 &&*/ IsConstructible(children[0])); // TODO
    importer.first = children[0];
  }

  std::unique_ptr<kpi::INode> fileState{
      dynamic_cast<kpi::INode*>(SpawnState(importer.first).release())};
  if (!fileState.get()) {
    printf("Cannot spawn file state %s.\n", importer.first.c_str());
    return;
  }
  struct Handler : oishii::ErrorHandler {
    void onErrorBegin(const oishii::DataProvider& stream) override {
      puts("[Begin Error]");
    }
    void onErrorDescribe(const oishii::DataProvider& stream, const char* type,
                         const char* brief, const char* details) override {
      printf("- [Describe] Type %s, Brief: %s, Details: %s\n", type, brief,
             details);
    }
    void onErrorAddStackTrace(const oishii::DataProvider& stream,
                              std::streampos start, std::streamsize size,
                              const char* domain) override {
      printf("- [Stack] Start: %u, Size: %u, Domain: %s\n", (u32)start,
             (u32)size, domain);
    }
    void onErrorEnd(const oishii::DataProvider& stream) override {
      puts("[End Error]");
    }
  } handler;
  reader.addErrorHandler(&handler);

  importer.second->read_(*dynamic_cast<kpi::INode*>(fileState.get()),
                         provider.slice());

  auto edWindow =
      std::make_unique<EditorWindow>(std::move(fileState), reader.getFile());

  if (policy == OpenFilePolicy::NewEditor) {
    attachEditorWindow(std::move(edWindow));
  } else if (policy == OpenFilePolicy::ReplaceEditor) {
    //	if (getActiveEditor())
    //		detachWindow(getActiveEditor()->mId);
    attachEditorWindow(std::move(edWindow));
  } else if (policy == OpenFilePolicy::ReplaceEditorIfMatching) {
    //	if (getActiveEditor() &&
    //((EditorWindow*)getActiveEditor())->mState->mName.namespacedId ==
    // fileState->mName.namespacedId)
    //	{
    //		detachWindow(getActiveEditor()->mId);
    //	}

    attachEditorWindow(std::move(edWindow));
  } else {
    throw "Invalid open file policy";
  }
}
void RootWindow::attachEditorWindow(std::unique_ptr<EditorWindow> editor) {
  mAttachEditorsQueue.push(editor->getName());
  attachWindow(std::move(editor));
}

RootWindow::RootWindow() : Applet("RiiStudio " RII_TIME_STAMP) {
#ifdef _WIN32
  glDebugMessageCallback(GlCallback, 0);
#endif

  InitAPI();
}
RootWindow::~RootWindow() { DeinitAPI(); }

void RootWindow::save(const std::string& _path) {
  std::string path = _path;
  if (path.ends_with(".bdl")) {
    path.resize(path.size() - 4);
    path += ".bmd";
  }
  EditorWindow* ed =
      dynamic_cast<EditorWindow*>(getActive() ? getActive() : nullptr);
  if (!ed)
    return;

  oishii::Writer writer(1024);

  auto ex = SpawnExporter(*ed->mState.get());
  if (!ex) {
    DebugReport("Failed to spawn importer.\n");
    return;
  }
  ex->write_(*ed->mState.get(), writer);

#ifdef _WIN32
  std::ofstream stream(path, std::ios::binary | std::ios::out);
  stream.write((const char*)writer.getDataBlockStart(), writer.getBufSize());
#else
  static_assert(sizeof(void*) == sizeof(u32), "emscripten pointer size");

  EM_ASM({ window.Module.downloadBuffer($0, $1, $2, $3); },
         reinterpret_cast<u32>(writer.getDataBlockStart()), writer.getBufSize(),
         reinterpret_cast<u32>(path.c_str()), path.size());
#endif
}
void RootWindow::saveAs() {
  auto results = pfd::save_file("Save File", "", {"All Files", "*"}).result();
  if (results.empty())
    return;

  save(results);
}

} // namespace riistudio::frontend
