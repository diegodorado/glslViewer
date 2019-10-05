#include "gltf.h"

#include <iostream>
#include <fstream>
#include <string>
#include <map>

#include "pixels.h"
#include "rgbe/rgbe.h" 

#include "../gl/vbo.h"
#include "../tools/fs.h"
#include "../tools/geom.h"
#include "../tools/text.h"

#define TINYGLTF_IMPLEMENTATION
#define STB_IMAGE_IMPLEMENTATION
#define STB_IMAGE_WRITE_IMPLEMENTATION
// #define TINYGLTF_NOEXCEPTION
// #define JSON_NOEXCEPTION
#include "tinygltf/tiny_gltf.h"

#define BUFFER_OFFSET(i) ((char *)NULL + (i))

bool loadModel(tinygltf::Model& _model, const std::string& _filename) {
    tinygltf::TinyGLTF loader;
    std::string err;
    std::string warn;
    std::string ext = getExt(_filename);

    bool res = false;

    // assume binary glTF.
    if (ext == "glb" || ext == "GLB")
        res = loader.LoadBinaryFromFile(&_model, &err, &warn, _filename.c_str());

    // assume ascii glTF.
    else
        res = loader.LoadASCIIFromFile(&_model, &err, &warn, _filename.c_str());

    if (!warn.empty())
        std::cout << "Warn: " << warn.c_str() << std::endl;

    if (!err.empty())
        std::cout << "ERR: " << err.c_str() << std::endl;

    return res;
}

GLenum extractMode(const tinygltf::Primitive& _primitive) {
    if (_primitive.mode == TINYGLTF_MODE_TRIANGLES) {
      return GL_TRIANGLES;
    } else if (_primitive.mode == TINYGLTF_MODE_TRIANGLE_STRIP) {
      return GL_TRIANGLE_STRIP;
    } else if (_primitive.mode == TINYGLTF_MODE_TRIANGLE_FAN) {
      return GL_TRIANGLE_FAN;
    } else if (_primitive.mode == TINYGLTF_MODE_POINTS) {
      return GL_POINTS;
    } else if (_primitive.mode == TINYGLTF_MODE_LINE) {
      return GL_LINES;
    } else if (_primitive.mode == TINYGLTF_MODE_LINE_LOOP) {
      return GL_LINE_LOOP;
    } else {
      return 0;
    }
}

void extractIndices(const tinygltf::Model& _model, const tinygltf::Accessor& _indexAccessor, Mesh& _mesh) {
    const tinygltf::BufferView &buffer_view = _model.bufferViews[_indexAccessor.bufferView];
    const tinygltf::Buffer &buffer = _model.buffers[buffer_view.buffer];
    const uint8_t* base = &buffer.data.at(buffer_view.byteOffset + _indexAccessor.byteOffset);

    switch (_indexAccessor.componentType) {
        case TINYGLTF_COMPONENT_TYPE_UNSIGNED_INT: {
            const uint32_t *p = (uint32_t*) base;
            for (size_t i = 0; i < _indexAccessor.count; ++i) {
                _mesh.addIndex( p[i] );
            }
        }; break;
        case TINYGLTF_COMPONENT_TYPE_UNSIGNED_SHORT: {
            const uint16_t *p = (uint16_t*) base;
            for (size_t i = 0; i < _indexAccessor.count; ++i) {
                _mesh.addIndex( p[i] );
            }
        }; break;
        case TINYGLTF_COMPONENT_TYPE_UNSIGNED_BYTE: {
            const uint8_t *p = (uint8_t*) base;
            for (size_t i = 0; i < _indexAccessor.count; ++i) {
                _mesh.addIndex( p[i] );
            }
        }; break;
    }
}

void extractVertexData(uint32_t v_pos, const uint8_t *base, int accesor_componentType, int accesor_type, bool accesor_normalized, uint32_t byteStride, float *output, uint8_t max_num_comp) {
    float v[4] = {0.0f, 0.0f, 0.0f, 0.0f};
    uint32_t ncomp = 1;
    switch (accesor_type) {
        case TINYGLTF_TYPE_SCALAR: ncomp = 1; break;
        case TINYGLTF_TYPE_VEC2:   ncomp = 2; break;
        case TINYGLTF_TYPE_VEC3:   ncomp = 3; break;
        case TINYGLTF_TYPE_VEC4:   ncomp = 4; break;
        default:
            assert(!"invalid type");
    }
    switch (accesor_componentType) {
        case TINYGLTF_COMPONENT_TYPE_FLOAT: {
            const float *data = (float*)(base+byteStride*v_pos);
            for (uint32_t i = 0; (i < ncomp); ++i) {
                v[i] = data[i];
            }
        }
        // TODO SUPPORT OTHER FORMATS
        break;
        default:
            assert(!"Conversion Type from float to -> ??? not implemented yet");
            break;
    }
    for (uint32_t i = 0; i < max_num_comp; ++i) {
        output[i] = v[i];
    }
}

Material extractMaterial(const tinygltf::Model& _model, const tinygltf::Material& _material, Uniforms& _uniforms, bool _verbose) {
    int texCounter = 0;
    Material mat;
    mat.name = toLower( toUnderscore( purifyString( _material.name ) ) );

    mat.addDefine("MATERIAL_NAME_" + toUpper(mat.name) );
    mat.addDefine("MATERIAL_BASECOLOR", (double*)_material.pbrMetallicRoughness.baseColorFactor.data(), 4);
    if (_material.pbrMetallicRoughness.baseColorTexture.index >= 0) {
        const tinygltf::Texture &tex = _model.textures[_material.pbrMetallicRoughness.baseColorTexture.index];
        const tinygltf::Image &image = _model.images[tex.source];
        std::string name = image.name + image.uri;
        if (name.empty())
            name = "texture" + toString(texCounter++);
        name = getUniformName(name);

        if (_verbose)
            std::cout << "Loading " << name << "for BASECOLORMAP as " << name << std::endl;

        Texture* texture = new Texture();
        texture->load(image.width, image.height, image.component, image.bits, &image.image.at(0));
        if (!_uniforms.addTexture(name, texture)) {
            delete texture;
        }
        mat.addDefine("MATERIAL_BASECOLORMAP", name);
    }

    mat.addDefine("MATERIAL_EMISSIVE", (double*)_material.emissiveFactor.data(), 3);
    if (_material.emissiveTexture.index >= 0) {
        const tinygltf::Image &image = _model.images[_model.textures[_material.emissiveTexture.index].source];
        std::string name = image.name + image.uri;
        if (name.empty())
            name = "texture" + toString(texCounter++);
        name = getUniformName(name);

        if (_verbose)
            std::cout << "Loading " << name << "for EMISSIVEMAP as " << name << std::endl;

        Texture* texture = new Texture();
        texture->load(image.width, image.height, image.component, image.bits, &image.image.at(0));
        if (!_uniforms.addTexture(name, texture)) {
            delete texture;
        }
        mat.addDefine("MATERIAL_EMISSIVEMAP", name);
    }

    mat.addDefine("MATERIAL_ROUGHNESS", _material.pbrMetallicRoughness.roughnessFactor);
    mat.addDefine("MATERIAL_METALLIC", _material.pbrMetallicRoughness.metallicFactor);
    if (_material.pbrMetallicRoughness.metallicRoughnessTexture.index >= 0) {
        tinygltf::Texture tex = _model.textures[_material.pbrMetallicRoughness.metallicRoughnessTexture.index];
        const tinygltf::Image &image = _model.images[tex.source];
        std::string name = image.name + image.uri;
        if (name.empty())
            name = "texture" + toString(texCounter++);
        name = getUniformName(name);

        if (_verbose)
            std::cout << "Loading " << name << "for METALLICROUGHNESSMAP as " << name << std::endl;

        Texture* texture = new Texture();
        texture->load(image.width, image.height, image.component, image.bits, &image.image.at(0));
        if (!_uniforms.addTexture(name, texture)) {
            delete texture;
        }
        mat.addDefine("MATERIAL_METALLICROUGHNESSMAP", name);
    }

    // NORMALMAP
    if (_material.normalTexture.index >= 0) {
        const tinygltf::Image &image = _model.images[_model.textures[_material.normalTexture.index].source];
        std::string name = image.name + image.uri;
        if (name.empty())
            name = "texture" + toString(texCounter++);
        name = getUniformName(name);

        if (_verbose)
            std::cout << "Loading " << name << "for NORMALMAP as " << name << std::endl;

        Texture* texture = new Texture();
        texture->load(image.width, image.height, image.component, image.bits, &image.image.at(0));
        if (!_uniforms.addTexture(name, texture)) {
            delete texture;
        }
        mat.addDefine("MATERIAL_NORMALMAP", name);

        if (_material.normalTexture.scale != 1.0)
            mat.addDefine("MATERIAL_NORMALMAP_SCALE", glm::vec3(_material.normalTexture.scale, _material.normalTexture.scale, 1.0));
    }

    // OCCLUSION
    if (_material.occlusionTexture.index >= 0) {
        const tinygltf::Image &image = _model.images[_model.textures[_material.occlusionTexture.index].source];
        std::string name = image.name + image.uri;
        if (name.empty())
            name = "texture" + toString(texCounter++);
        name = getUniformName(name);

        if (_verbose)
            std::cout << "Loading " << name << "for OCCLUSIONMAP as " << name << std::endl;

        Texture* texture = new Texture();
        texture->load(image.width, image.height, image.component, image.bits, &image.image.at(0));
        if (!_uniforms.addTexture(name, texture)) {
            delete texture;
        }
        mat.addDefine("MATERIAL_OCCLUSIONMAP", name);

        if (_material.occlusionTexture.strength != 1.0)
            mat.addDefine("MATERIAL_OCCLUSIONMAP_STRENGTH", _material.occlusionTexture.strength);
    }

    return mat;
}

void extractMesh(const tinygltf::Model& _model, const tinygltf::Mesh& _mesh, Node _currentProps, Uniforms& _uniforms, Models& _models, bool _verbose) {
    if (_verbose)
        std::cout << "  Parsing Mesh " << _mesh.name << std::endl;

    for (size_t i = 0; i < _mesh.primitives.size(); ++i) {
        if (_verbose)
            std::cout << "   primitive " << i + 1 << "/" << _mesh.primitives.size() << std::endl;

        const tinygltf::Primitive &primitive = _mesh.primitives[i];

        Mesh mesh;
        if (primitive.indices >= 0)
            extractIndices(_model, _model.accessors[primitive.indices], mesh);
        mesh.setDrawMode(extractMode(primitive));

        // Extract Vertex Data
        for (auto &attrib : primitive.attributes) {
            const tinygltf::Accessor &accessor = _model.accessors[attrib.second];
            const tinygltf::BufferView &bufferView = _model.bufferViews[accessor.bufferView];
            const tinygltf::Buffer &buffer = _model.buffers[bufferView.buffer];
            int byteStride = accessor.ByteStride(bufferView);

            if (attrib.first.compare("POSITION") == 0)  {
                for (size_t v = 0; v < accessor.count; v++) {
                    glm::vec3 pos;
                    extractVertexData(v, &buffer.data.at(bufferView.byteOffset + accessor.byteOffset), accessor.componentType, accessor.type, accessor.normalized, byteStride, &pos[0], 3);
                    mesh.addVertex(pos);
                }
            }

            else if (attrib.first.compare("COLOR_0") == 0)  {
                for (size_t v = 0; v < accessor.count; v++) {
                    glm::vec4 col = glm::vec4(1.0f);
                    extractVertexData(v, &buffer.data.at(bufferView.byteOffset + accessor.byteOffset), accessor.componentType, accessor.type, accessor.normalized, byteStride, &col[0], 4);
                    mesh.addColor(col);
                }
            }

            else if (attrib.first.compare("NORMAL") == 0)  {
                for (size_t v = 0; v < accessor.count; v++) {
                    glm::vec3 nor;
                    extractVertexData(v, &buffer.data.at(bufferView.byteOffset + accessor.byteOffset), accessor.componentType, accessor.type, accessor.normalized, byteStride, &nor[0], 3);
                    mesh.addNormal(nor);
                }
            }

            else if (attrib.first.compare("TEXCOORD_0") == 0)  {
                for (size_t v = 0; v < accessor.count; v++) {
                    glm::vec2 uv;
                    extractVertexData(v, &buffer.data.at(bufferView.byteOffset + accessor.byteOffset), accessor.componentType, accessor.type, accessor.normalized, byteStride, &uv[0], 2);
                    mesh.addTexCoord(uv);
                }
            }

            else if (attrib.first.compare("TANGENT") == 0)  {
                for (size_t v = 0; v < accessor.count; v++) {
                    glm::vec4 tan;
                    extractVertexData(v, &buffer.data.at(bufferView.byteOffset + accessor.byteOffset), accessor.componentType, accessor.type, accessor.normalized, byteStride, &tan[0], 4);
                    mesh.addTangent(tan);
                }
            }

            else {
                std::cout << " " << std::endl;
                std::cout << "Attribute: " << attrib.first << std::endl;
                std::cout << "  type        :" << accessor.type << std::endl;
                std::cout << "  component   :" << accessor.componentType << std::endl;
                std::cout << "  normalize   :" << accessor.normalized << std::endl;
                std::cout << "  bufferView  :" << accessor.bufferView << std::endl;
                std::cout << "  byteOffset  :" << accessor.byteOffset << std::endl;
                std::cout << "  count       :" << accessor.count << std::endl;
                std::cout << "  byteStride  :" << byteStride << std::endl;
                std::cout << " "<< std::endl;
            }
        }

        if (_verbose) {
            std::cout << "    vertices = " << mesh.getVertices().size() << std::endl;
            std::cout << "    colors   = " << mesh.getColors().size() << std::endl;
            std::cout << "    normals  = " << mesh.getNormals().size() << std::endl;
            std::cout << "    uvs      = " << mesh.getTexCoords().size() << std::endl;
            std::cout << "    indices  = " << mesh.getIndices().size() << std::endl;

            if (mesh.getDrawMode() == GL_TRIANGLES) {
                std::cout << "    triang.  = " << mesh.getIndices().size()/3 << std::endl;
            }
            else if (mesh.getDrawMode() == GL_LINES ) {
                std::cout << "    lines    = " << mesh.getIndices().size()/2 << std::endl;
            }
        }

        if ( !mesh.hasNormals() )
            if ( mesh.computeNormals() )
                if ( _verbose )
                    std::cout << "    . Compute normals" << std::endl;

        if ( mesh.computeTangents() )
            if ( _verbose )
                std::cout << "    . Compute tangents" << std::endl;

        Material mat = extractMaterial( _model, _model.materials[primitive.material], _uniforms, _verbose );

        Model* m = new Model(_mesh.name, mesh, mat); 
        m->setProperties(_currentProps);
        _models.push_back( m );

        // _models.push_back( new Model(_mesh.name, mesh, mat) );
    }
};

// bind models
void extractNodes(const tinygltf::Model& _model, const tinygltf::Node& _node, Node _currentProps, Uniforms& _uniforms, Models& _models, bool _verbose) {
    if (_verbose)
        std::cout << "Entering node " << _node.name << std::endl;

    if (_node.rotation.size() > 0)
        _currentProps.rotate( glm::quat(_node.rotation[0], _node.rotation[1], _node.rotation[2], _node.rotation[3]) );

    if (_node.scale.size() > 0)
        _currentProps.scale( glm::vec3(_node.scale[0], _node.scale[1], _node.scale[2]) );

    if (_node.translation.size() > 0)
        _currentProps.translate( glm::vec3(_node.translation[0], _node.translation[1], _node.translation[2]) );

    if (_node.matrix.size() > 0)
        _currentProps.apply( glm::mat4( _node.matrix[0],  _node.matrix[1],  _node.matrix[2],  _node.matrix[3],
                                        _node.matrix[4],  _node.matrix[5],  _node.matrix[6],  _node.matrix[7],
                                        _node.matrix[8],  _node.matrix[9],  _node.matrix[10], _node.matrix[11],
                                        _node.matrix[12], _node.matrix[13], _node.matrix[14], _node.matrix[15]) );
        // _currentProps.apply( glm::mat4( _node.matrix[0],  _node.matrix[4],  _node.matrix[8],  _node.matrix[12],
        //                                 _node.matrix[1],  _node.matrix[5],  _node.matrix[9],  _node.matrix[13],
        //                                 _node.matrix[2],  _node.matrix[6],  _node.matrix[10], _node.matrix[14],
        //                                 _node.matrix[3],  _node.matrix[7],  _node.matrix[11], _node.matrix[15]) );


    if (_node.mesh >= 0)
        extractMesh(_model, _model.meshes[ _node.mesh ], _currentProps, _uniforms, _models, _verbose);

    if (_node.camera >= 0)
        if (_verbose)
            std::cout << "  w camera" << std::endl;
        // TODO extract camera
    
    for (size_t i = 0; i < _node.children.size(); i++) {
        extractNodes(_model, _model.nodes[ _node.children[i] ], _currentProps, _uniforms, _models, _verbose);
    }
};

bool loadGLTF(Uniforms& _uniforms, WatchFileList& _files, Materials& _materials, Models& _models, int _index, bool _verbose) {
    tinygltf::Model model;
    std::string filename = _files[_index].path;

    if (! loadModel(model, filename)) {
        std::cout << "Failed to load .glTF : " << filename << std::endl;
        return false;
    }

    Node    root;
    const tinygltf::Scene &scene = model.scenes[model.defaultScene];
    for (size_t i = 0; i < scene.nodes.size(); ++i) {
        extractNodes(model, model.nodes[scene.nodes[i]], root, _uniforms, _models, _verbose);
    }

    return true;
}





template<typename T>
void flipPixelsVertically(T *_pixels, int _width, int _height, int _bytes_per_pixel) {
    const size_t stride = _width * _bytes_per_pixel;
    T *row = (T*)malloc(stride * sizeof(T));
    T *low = _pixels;
    T *high = &_pixels[(_height - 1) * stride];
    for (; low < high; low += stride, high -= stride) {
        memcpy(row, low, stride * sizeof(T));
        memcpy(low, high, stride * sizeof(T));
        memcpy(high, row, stride * sizeof(T));
    }
    free(row);
}

float* loadHDRFloatPixels(const std::string& _path, int *_width, int *_height, bool _vFlip) {
    FILE* file = fopen(_path.c_str(), "rb");
    RGBE_ReadHeader(file, _width, _height, NULL);

    float* pixels = new float[(*_width) * (*_height) * 3];
    RGBE_ReadPixels_RLE(file, pixels, *_width, *_height);
    
    if (_vFlip) {
        flipPixelsVertically<float>(pixels, (*_width), (*_height), 3);
    }

    fclose(file);
    return pixels;
}

uint16_t* loadPixels16(const std::string& _path, int *_width, int *_height, Channels _channels, bool _vFlip) {
    stbi_set_flip_vertically_on_load(_vFlip);
    int comp;
    uint16_t *pixels = stbi_load_16(_path.c_str(), _width, _height, &comp, _channels);
    return pixels;
}

unsigned char* loadPixels(const std::string& _path, int *_width, int *_height, Channels _channels, bool _vFlip) {
    stbi_set_flip_vertically_on_load(_vFlip);
    int comp;
    unsigned char* pixels = stbi_load(_path.c_str(), _width, _height, &comp, (_channels == RGB)? STBI_rgb : STBI_rgb_alpha);
    return pixels;
}

bool savePixels(const std::string& _path, unsigned char* _pixels, int _width, int _height) {

    // Flip the image on Y
    int depth = 4;
    flipPixelsVertically<unsigned char>(_pixels, _width, _height, depth);

    if (0 == stbi_write_png(_path.c_str(), _width, _height, 4, _pixels, _width * 4)) {
        std::cout << "can't create file " << _path << std::endl;
    }

    return true;
}
