#include <core/common.h>
#include <core/kpi/Node.hpp>

#include "collection.hpp"
#include "model.hpp"

namespace riistudio::g3d {

kpi::DecentralizedInstaller Installer([](kpi::ApplicationPlugins& installer) {
  installer.addType<g3d::Bone, libcube::IBoneDelegate>()
      .addType<g3d::Texture, libcube::Texture>()
      .addType<g3d::Material, libcube::IGCMaterial>()
      .addType<g3d::Model, lib3d::Model>()
      .addType<g3d::Collection, lib3d::Scene>()
      .addType<g3d::PositionBuffer>()
      .addType<g3d::NormalBuffer>()
      .addType<g3d::ColorBuffer>()
      .addType<g3d::TextureCoordinateBuffer>()
      .addType<g3d::Polygon, libcube::IndexedPolygon>();
});

} // namespace riistudio::g3d
