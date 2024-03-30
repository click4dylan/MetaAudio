#include <metahook.h>

#include "snd_local.h"
#include "Utilities/VectorUtils.hpp"
#include "Loaders/SteamAudioMapMeshLoader.hpp"
#include <bsprender.h>

namespace MetaAudio
{
  constexpr const float EPSILON = 0.000001f;

  bool VectorEquals(const alure::Vector3& left, const alure::Vector3& right)
  {
    return left[0] == right[0] && left[1] == right[1] && left[2] == right[2];
  }

  bool VectorApproximatelyEquals(const alure::Vector3& left, const alure::Vector3& right)
  {
    return (left[0] - right[0]) < EPSILON && (left[1] - right[1]) < EPSILON && (left[2] - right[2]) < EPSILON;
  }

  SteamAudioMapMeshLoader::SteamAudioMapMeshLoader(IPLhandle sa_context, IPLSimulationSettings simulSettings)
    : sa_simul_settings(simulSettings), sa_context(sa_context)
  {
#ifdef THREADED_MESH_LOADER
    update_mutex.lock();
#endif
    current_map = std::make_unique<ProcessedMap>("", nullptr, nullptr, nullptr);
#ifdef THREADED_MESH_LOADER
    update_mutex.unlock();
#endif
  }

  alure::Vector3 SteamAudioMapMeshLoader::Normalize(const alure::Vector3& vector)
  {
    float length = vector.getLength();
    if (length == 0)
    {
      return alure::Vector3(0, 0, 1);
    }
    length = 1 / length;
    return alure::Vector3(vector[0] * length, vector[1] * length, vector[2] * length);
  }

  float SteamAudioMapMeshLoader::DotProduct(const alure::Vector3& left, const alure::Vector3& right)
  {
    return left[0] * right[0] + left[1] * right[1] + left[2] * right[2];
  }

  std::vector<BSPLine> bsp_wires;

  // let bond.exe access this for rendering directly for testing
  extern "C" __declspec(dllexport) std::vector<BSPLine>& GetMapAudioMesh()
  {
      return bsp_wires;
  }

  void SteamAudioMapMeshLoader::drawmesh()
  {
      int drawn = 0;
      int i = 0;
      for (auto& l : bsp_wires)
      {
          if (drawn == 3)
          {
              if (i % 3 == 0 && rand() % 150 == 0)
                  drawn = 0;
          }
          if (drawn < 3)
          {
              g_pEngfuncs->pEffectsAPI->R_TracerEffect(&l.start[0], &l.end[0]);
              ++drawn;
          }
          ++i;
      }
  }

#ifdef THREADED_MESH_LOADER
  void SteamAudioMapMeshLoader::async_update(SteamAudioMapMeshLoader* thisptr, model_s* mapModel)
  {
      thisptr->update_mutex.lock();

      std::vector<IPLTriangle> triangles;
      triangles.reserve(mapModel->numindices / 3);
      std::vector<IPLVector3> vertices;

#if 1
      bsp_wires.clear();
#endif

      for (int i = 0; i < mapModel->nummodelsurfaces; ++i)
      {
          msurface_t* surf = &mapModel->surfaces[mapModel->firstmodelsurface + i];

          if (!surf->parent_face)
              continue;

          int firstvertex = surf->parent_face->firstVertex;
          int firstindex = surf->parent_face->firstIndex;
          int numindices = surf->parent_face->numIndices;
          int numvertexes = surf->parent_face->numVertices;
          std::vector<alure::Vector3> surfaceVerts;
          surfaceVerts.reserve(numvertexes);

          // nightfire face render method
#if 1

          int numTriangles = numindices / 3;
          for (int i = 0; i < numTriangles; ++i)
          {
              vec3_t& coord1 = mapModel->verts[firstvertex + mapModel->indices[firstindex + (i * 3)]];
              vec3_t& coord2 = mapModel->verts[firstvertex + mapModel->indices[firstindex + (i * 3) + 1]];
              vec3_t& coord3 = mapModel->verts[firstvertex + mapModel->indices[firstindex + (i * 3) + 2]];

              surfaceVerts.emplace_back(MetaAudio::AL_UnpackVector(coord1));
              surfaceVerts.emplace_back(MetaAudio::AL_UnpackVector(coord2));
              surfaceVerts.emplace_back(MetaAudio::AL_UnpackVector(coord3));

              //bsp_wires.emplace_back(coord1, coord2, 255, 0, 0, 200);
              //bsp_wires.emplace_back(coord2, coord3, 0, 255, 0, 200);
              //bsp_wires.emplace_back(coord3, coord1, 0, 0, 255, 200);
          }
#if 0
          //wind around the face
          vec3_t* vertex = &mapModel->verts[firstvertex];
          int* indices = (int*)&mapModel->indices[firstindex];
          int indexes_processed = 0;
          vec3_t first_coord;
          vec3_t last_coord;
          while (indexes_processed < numindices)
          {
              vec3_t coord;
              int draw_index = 2;
              for (int i = 0; i < 3; ++i)
              {
                  //int index = indices[i];
                  //coord[i] = (*vertex)[index];
                  ++indexes_processed;
              }

              coord[0] = (*vertex)[0];
              coord[1] = (*vertex)[1];
              coord[2] = (*vertex)[2];

              if (indexes_processed == 3)
              {
                  // new line start pos
                  first_coord[0] = last_coord[0] = coord[0];
                  first_coord[1] = last_coord[1] = coord[1];
                  first_coord[2] = last_coord[2] = coord[2];
              }
              else
              {
                  // next line pos
                  bsp_wires.emplace_back(last_coord, coord);
                  last_coord[0] = coord[0];
                  last_coord[1] = coord[1];
                  last_coord[2] = coord[2];
              }

              indices += 3;

              if (++vertex >= &mapModel->verts[firstvertex + numvertexes])
                  break; // never gets hit, thankfully
          }

          if (indexes_processed)
              bsp_wires.emplace_back(last_coord, first_coord);
#endif
#endif

          // triangulation

          // Get rid of duplicate vertices
          surfaceVerts.erase(std::unique(surfaceVerts.begin(), surfaceVerts.end(), VectorEquals), surfaceVerts.end());

          // Skip invalid face
          if (surfaceVerts.size() < 3)
          {
              continue;
          }

          // Triangulate
          alure::Vector3 origin{ 0,0,0 };
          alure::Vector<IPLVector3> newVerts;
          newVerts.reserve(surfaceVerts.size());
          { // remove colinear
              for (size_t i = 0; i < surfaceVerts.size(); ++i)
              {
                  auto vertexBefore = origin + surfaceVerts[(i > 0) ? (i - 1) : (surfaceVerts.size() - 1)];
                  auto vertex = origin + surfaceVerts[i];
                  auto vertexAfter = origin + surfaceVerts[(i < (surfaceVerts.size() - 1)) ? (i + 1) : 0];

                  auto v1 = thisptr->Normalize(vertexBefore - vertex);
                  auto v2 = thisptr->Normalize(vertexAfter - vertex);

                  float vertDot = thisptr->DotProduct(v1, v2);
                  if (std::fabs(vertDot + 1.f) < EPSILON)
                  {
                      // colinear, drop it
                  }
                  else
                  {
                      newVerts.emplace_back<IPLVector3>({ vertex[0], vertex[1], vertex[2] });
                  }
              }
          }

          // Skip invalid face, it is just a line
          if (newVerts.size() < 3)
          {
              continue;
          }

          { // generate indices
              int indexoffset = vertices.size();

              for (size_t i = 0; i < newVerts.size() - 2; ++i)
              {
#ifdef WINXP
                  //vs2015 xp
                  IPLTriangle tri = { indexoffset + i + 2 , indexoffset + i + 1, indexoffset };
                  triangles.push_back(tri);
#else
                  auto& triangle = triangles.emplace_back();

                  triangle.indices[0] = indexoffset + i + 2;
                  triangle.indices[1] = indexoffset + i + 1;
                  triangle.indices[2] = indexoffset;
#endif
              }

              // Add vertices to final array
              vertices.insert(vertices.end(), newVerts.begin(), newVerts.end());
          }
      }

      IPLhandle scene = nullptr;
      IPLerror error = gSteamAudio.iplCreateScene(thisptr->sa_context, nullptr, IPLSceneType::IPL_SCENETYPE_PHONON, thisptr->materials.size(), thisptr->materials.data(), nullptr, nullptr, nullptr, nullptr, nullptr, &scene);
      if (error)
      {
          throw std::runtime_error("Error creating scene: " + std::to_string(error));
      }

      IPLhandle staticmesh = nullptr;
      error = gSteamAudio.iplCreateStaticMesh(scene, vertices.size(), triangles.size(), vertices.data(), triangles.data(), std::vector<int>(triangles.size(), 0).data(), &staticmesh);
      if (error)
      {
          throw std::runtime_error("Error creating static mesh: " + std::to_string(error));
      }

      IPLhandle env = nullptr;
      error = gSteamAudio.iplCreateEnvironment(thisptr->sa_context, nullptr, thisptr->sa_simul_settings, scene, nullptr, &env);
      if (error)
      {
          throw std::runtime_error("Error creating environment: " + std::to_string(error));
      }
      thisptr->current_map = std::make_unique<ProcessedMap>(mapModel->name, env, scene, staticmesh);

      thisptr->update_mutex.unlock();
  }
#endif

  bool SteamAudioMapMeshLoader::update()
  {
    // check map

    bool paused = false;
    {
      cl_entity_s* map = g_pEngfuncs->GetEntityByIndex(0);
      if (map == nullptr ||
          map->model == nullptr ||
          !map->model->isloaded ||
          g_pEngfuncs->GetLevelName() == nullptr ||
          _stricmp(g_pEngfuncs->GetLevelName(), map->model->name) != 0)
      {
        paused = true;
      }
      else
      {
#ifdef THREADED_MESH_LOADER
        if (!update_mutex.try_lock())
            return false;
#endif

        const auto mapModel = map->model;
        if (current_map->Name() == mapModel->name)
        {
#ifdef THREADED_MESH_LOADER
            update_mutex.unlock();
#endif
            return true;
        }

        static ConsoleVariable* room_type = g_pEngfuncs->GetConsoleVariableClient("room_type");
        if (room_type)
            room_type->setValueInt(0);
#ifdef THREADED_MESH_LOADER
        std::thread async(SteamAudioMapMeshLoader::async_update, this, mapModel);
        async.detach();
        update_mutex.unlock();
#else
        std::vector<IPLTriangle> triangles;
        std::vector<IPLVector3> vertices;

#if 1
        bsp_wires.clear();
#endif

        for (int i = 0; i < mapModel->nummodelsurfaces; ++i)
        {
            std::vector<alure::Vector3> surfaceVerts;
            msurface_t* surf = &mapModel->surfaces[mapModel->firstmodelsurface + i];

            if (!surf->parent_face)
                continue;

            int firstvertex = surf->parent_face->firstVertex;
            int numvertexes = surf->parent_face->numVertices;
            int firstindex = surf->parent_face->firstIndex;
            int numindices = surf->parent_face->numIndices;

            // nightfire face render method
#if 1
            vec3_t* vertex = &mapModel->verts[firstvertex];
            int* indices = (int*)&mapModel->indices[firstindex];
            int indexes_processed = 0;
            vec3_t first_coord;
            vec3_t last_coord;
            int numTriangles = numindices / 3;
            for (int i = 0; i < numTriangles; ++i) {
                vec3_t& coord1 = mapModel->verts[firstvertex + mapModel->indices[firstindex + (i * 3)]];
                vec3_t& coord2 = mapModel->verts[firstvertex + mapModel->indices[firstindex + (i * 3) + 1]];
                vec3_t& coord3 = mapModel->verts[firstvertex + mapModel->indices[firstindex + (i * 3) + 2]];

                //bsp_wires.emplace_back(coord1, coord2);
                //bsp_wires.emplace_back(coord2, coord3);
                //bsp_wires.emplace_back(coord3, coord1);

                // Do something with these three coords
            }
#if 0
            //wind around the face
            vec3_t* vertex = &mapModel->verts[firstvertex];
            int* indices = (int*)&mapModel->indices[firstindex];
            int indexes_processed = 0;
            vec3_t first_coord;
            vec3_t last_coord;
            while (indexes_processed < numindices)
            {
                vec3_t coord;
                int draw_index = 2;
                for (int i = 0; i < 3; ++i)
                {
                    //int index = indices[i];
                    //coord[i] = (*vertex)[index];
                    ++indexes_processed;
                }

                coord[0] = (*vertex)[0];
                coord[1] = (*vertex)[1];
                coord[2] = (*vertex)[2];

                if (indexes_processed == 3)
                {
                    // new line start pos
                    first_coord[0] = last_coord[0] = coord[0];
                    first_coord[1] = last_coord[1] = coord[1];
                    first_coord[2] = last_coord[2] = coord[2];
                }
                else
                {
                    // next line pos
                    bsp_wires.emplace_back(last_coord, coord);
                    last_coord[0] = coord[0];
                    last_coord[1] = coord[1];
                    last_coord[2] = coord[2];
                }

                indices += 3;

                if (++vertex >= &mapModel->verts[firstvertex + numvertexes])
                    break; // never gets hit, thankfully
            }

            if (indexes_processed)
                bsp_wires.emplace_back(last_coord, first_coord);
#endif
#endif

            { // generate indices
                int indexoffset = vertices.size();

                int* indices = (int*)&mapModel->indices[firstindex];
                for (size_t i = 0; i < numindices; i += 3)
                {
                    auto& triangle = triangles.emplace_back();
                    triangle.indices[0] = indexoffset + indices[2];
                    triangle.indices[1] = indexoffset + indices[1];
                    triangle.indices[2] = indexoffset + indices[0];
                    indices += 3;
                }

                // Add vertices to final array
                vec3_t* vertex = &mapModel->verts[firstvertex];
                for (int i = 0; i < numvertexes; ++i)
                {
                    auto& verts = vertices.emplace_back();
                    verts.x = (*vertex)[0];
                    verts.y = (*vertex)[1];
                    verts.z = (*vertex)[2];
                    ++vertex;
                }
            }
        }

        IPLhandle scene = nullptr;
        IPLerror error = gSteamAudio.iplCreateScene(sa_context, nullptr, IPLSceneType::IPL_SCENETYPE_PHONON, materials.size(), materials.data(), nullptr, nullptr, nullptr, nullptr, nullptr, &scene);
        if (error)
        {
            throw std::runtime_error("Error creating scene: " + std::to_string(error));
        }

        IPLhandle staticmesh = nullptr;
        error = gSteamAudio.iplCreateStaticMesh(scene, vertices.size(), triangles.size(), vertices.data(), triangles.data(), std::vector<int>(triangles.size(), 0).data(), &staticmesh);
        if (error)
        {
            throw std::runtime_error("Error creating static mesh: " + std::to_string(error));
        }

        IPLhandle env = nullptr;
        error = gSteamAudio.iplCreateEnvironment(sa_context, nullptr, sa_simul_settings, scene, nullptr, &env);
        if (error)
        {
            throw std::runtime_error("Error creating environment: " + std::to_string(error));
        }
        current_map = std::make_unique<ProcessedMap>(mapModel->name, env, scene, staticmesh);
#endif
        return false;
      }
    }
  }

  IPLhandle SteamAudioMapMeshLoader::CurrentEnvironment()
  {
    IPLhandle result = nullptr;
#ifdef THREADED_MESH_LOADER
    if (update_mutex.try_lock())
    {
        result = current_map->Env();
        update_mutex.unlock();
    }
#else
    result = current_map->Env();
#endif
    return result;
  }
}