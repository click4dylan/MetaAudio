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
    current_map = std::make_unique<ProcessedMap>("", nullptr, nullptr, nullptr);
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

  // temporary
  void recursively_add_verts(std::vector<mface_t*> & faces_used, std::vector<alure::Vector3>& vertices, model_t* model, mbrush_t* brush)
  {
        for (int i = 0; i < brush->numsides; ++i)
        {
            mbrushside_t* side = &brush->sides[i];
            msurface_t* surf = side->surface;
            if (!surf)
                continue;

            if (surf->parent_face)
            {
                int vertex = surf->parent_face->firstVertex;
                int numvertexes = surf->parent_face->numVertices;

                auto found = std::find(faces_used.begin(), faces_used.end(), surf->parent_face);
                if (found == faces_used.end())
                {
                    for (int i = 0; i < numvertexes; ++i)
                    {
                        vertices.emplace_back(model->verts[vertex + i]);
                    }
                    faces_used.emplace_back(surf->parent_face);
                }
                else
                {
                    int alreadyadded = 1;
                }
            }
            else if (surf->parent_brush)
            {
                //recursively_add_verts(vertices, model, surf->parent_brush);
            }
            else
            {
                int wtf = 1;
            }
        }
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

  void SteamAudioMapMeshLoader::update()
  {
    // draw mesh
    //drawmesh();

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
        auto mapModel = map->model;
        if (current_map->Name() == mapModel->name)
        {
          return;
        }

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
      }
    }
  }

  IPLhandle SteamAudioMapMeshLoader::CurrentEnvironment()
  {
    return current_map->Env();
  }
}